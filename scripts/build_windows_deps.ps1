# Builds the native dependencies of the Windows app, on Windows (run once):
#   - nlohmann/json and stb headers           -> third_party/nlohmann, third_party/stb (if missing)
#   - OpenCascade 7.9.3 as static libraries   -> third_party/occt-build/windows-x64-install
#   - ANGLE (OpenGL ES 3 over Direct3D 11)    -> third_party/vcpkg/installed/x64-windows
#
# Run from a "x64 Native Tools Command Prompt for VS 2022" (or "Developer PowerShell
# for VS 2022" with -Arch amd64), in the repository:
#   powershell -ExecutionPolicy Bypass -File scripts\build_windows_deps.ps1
# Needs: Visual Studio 2022 (Desktop development with C++), CMake 3.22+, Ninja, Git.
# Takes about an hour (OpenCascade ~40 min, ANGLE ~15 min).
param([switch]$Clean)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..").Path
$Third = Join-Path $Root "third_party"
$Src = Join-Path $Third "occt-src"
$Work = Join-Path $Third "occt-build"
$Install = Join-Path $Work "windows-x64-install"
$Build = Join-Path $Work "windows-x64"

function Require($exe, $hint) {
    if (-not (Get-Command $exe -ErrorAction SilentlyContinue)) { throw "$exe not found: $hint" }
}
Require "cl.exe" "run this from the 'x64 Native Tools Command Prompt for VS 2022'."
Require "cmake" "install CMake 3.22+ (it ships with Visual Studio's C++ workload, or winget install Kitware.CMake)."
Require "ninja" "install Ninja (ships with Visual Studio's C++ CMake tools, or winget install Ninja-build.Ninja)."
Require "git" "install Git (winget install Git.Git)."

# cl.exe must target x64 (the x86 prompt builds 32-bit libraries that won't link).
$clBanner = (& cl.exe 2>&1 | Out-String)
if ($clBanner -notmatch "x64") { throw "cl.exe is not the x64 compiler. Use the 'x64 Native Tools Command Prompt for VS 2022'." }

New-Item -ItemType Directory -Force -Path $Third | Out-Null

# ---- Header-only libraries used by AppCore (also present in a full checkout) ----
$Json = Join-Path $Third "nlohmann\json.hpp"
if (-not (Test-Path $Json)) {
    Write-Host "Fetching nlohmann/json 3.11.3..."
    New-Item -ItemType Directory -Force -Path (Split-Path $Json) | Out-Null
    Invoke-WebRequest -UseBasicParsing "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" -OutFile $Json
}
$Stb = Join-Path $Third "stb"
foreach ($h in @("stb_image.h", "stb_image_write.h")) {
    $p = Join-Path $Stb $h
    if (-not (Test-Path $p)) {
        Write-Host "Fetching $h..."
        New-Item -ItemType Directory -Force -Path $Stb | Out-Null
        # stb master carries stb_image 2.30 / stb_image_write 1.16, the versions the Apple build uses.
        Invoke-WebRequest -UseBasicParsing "https://raw.githubusercontent.com/nothings/stb/master/$h" -OutFile $p
    }
}

# ---- OpenCascade sources (the macOS checkout isn't committed) ----
if (-not (Test-Path (Join-Path $Src "CMakeLists.txt"))) {
    Write-Host "Fetching OpenCascade 7.9.3 sources..."
    git clone --depth 1 --branch V7_9_3 https://github.com/Open-Cascade-SAS/OCCT.git $Src
    if ($LASTEXITCODE -ne 0) { throw "git clone of OpenCascade failed" }
}

if ($Clean -and (Test-Path $Build)) { Remove-Item -Recurse -Force $Build }

# ---- OpenCascade, same module set as scripts/build_occt.sh ----
# INSTALL_DIR_LAYOUT=Unix gives include/opencascade + lib, like the other platforms.
if (-not (Test-Path (Join-Path $Install "include\opencascade\Standard.hxx"))) {
    cmake -S $Src -B $Build -G Ninja `
        -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl `
        -DBUILD_LIBRARY_TYPE=Static `
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL `
        -DINSTALL_DIR_LAYOUT=Unix `
        -DBUILD_MODULE_Draw=OFF -DBUILD_MODULE_DETools=OFF -DBUILD_MODULE_Visualization=OFF `
        -DBUILD_MODULE_ApplicationFramework=ON -DBUILD_MODULE_DataExchange=ON `
        -DBUILD_DOC_Overview=OFF -DBUILD_SAMPLES_QT=OFF `
        -DUSE_FREETYPE=OFF -DUSE_FREEIMAGE=OFF -DUSE_OPENGL=OFF -DUSE_GLES2=OFF -DUSE_D3D=OFF -DUSE_TK=OFF -DUSE_TCL=OFF `
        -DUSE_RAPIDJSON=OFF -DUSE_DRACO=OFF -DUSE_TBB=OFF -DUSE_VTK=OFF -DUSE_FFMPEG=OFF -DUSE_OPENVR=OFF `
        "-DINSTALL_DIR=$Install" "-DCMAKE_INSTALL_PREFIX=$Install"
    if ($LASTEXITCODE -ne 0) { throw "OpenCascade configure failed" }
    cmake --build $Build --parallel
    if ($LASTEXITCODE -ne 0) { throw "OpenCascade build failed" }
    cmake --install $Build
    if ($LASTEXITCODE -ne 0) { throw "OpenCascade install failed" }
}
if (-not (Test-Path (Join-Path $Install "lib\TKernel.lib"))) {
    Write-Warning "TKernel.lib is not in $Install\lib; AppCore also searches win64\vc14\lib. Check the OpenCascade install layout."
}

# ---- ANGLE via a project-local vcpkg ----
$Vcpkg = Join-Path $Third "vcpkg"
if (-not (Test-Path (Join-Path $Vcpkg "vcpkg.exe"))) {
    if (-not (Test-Path $Vcpkg)) {
        git clone --depth 1 https://github.com/microsoft/vcpkg.git $Vcpkg
        if ($LASTEXITCODE -ne 0) { throw "git clone of vcpkg failed" }
    }
    & (Join-Path $Vcpkg "bootstrap-vcpkg.bat") -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw "vcpkg bootstrap failed" }
}
& (Join-Path $Vcpkg "vcpkg.exe") install angle:x64-windows --disable-metrics
if ($LASTEXITCODE -ne 0) { throw "vcpkg install angle:x64-windows failed" }

$Angle = Join-Path $Vcpkg "installed\x64-windows"
foreach ($f in @("include\EGL\egl.h", "include\GLES3\gl3.h", "lib\libEGL.lib", "lib\libGLESv2.lib", "bin\libEGL.dll", "bin\libGLESv2.dll")) {
    if (-not (Test-Path (Join-Path $Angle $f))) { Write-Warning "ANGLE: $f is missing under $Angle" }
}

Write-Host ""
Write-Host "OpenCascade: $Install"
Write-Host "ANGLE:       $Angle"
Write-Host "Next: scripts\build_windows.ps1"
