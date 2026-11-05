# Builds the Windows app: sculpt.dll (CMake + MSVC: AppCore, OpenCascade, ANGLE
# viewport host) and the WinUI 3 front end (dotnet), then copies the native
# DLLs next to Sculpt3D.exe.
#
#   powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 [-Configuration Release|Debug] [-Run] [-Publish] [-SkipNative]
#
# Run scripts\build_windows_deps.ps1 once first, from the same
# "x64 Native Tools Command Prompt for VS 2022".
#   -Publish     also writes a self-contained folder to dist\Sculpt3D-win-x64 (zip it to ship)
#   -SkipNative  reuse an existing build\windows-native\appcore\sculpt.dll
param(
    [ValidateSet("Release", "Debug")][string]$Configuration = "Release",
    [switch]$Run,
    [switch]$Publish,
    [switch]$SkipNative
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..").Path
$Native = Join-Path $Root "build\windows-native"
$Angle = Join-Path $Root "third_party\vcpkg\installed\x64-windows\bin"
$Project = Join-Path $Root "Windows\Sculpt3D\Sculpt3D.csproj"

if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) { throw "dotnet not found: install the .NET 8 SDK (winget install Microsoft.DotNet.SDK.8)." }
if (-not ((& dotnet --list-sdks) -match "^8\.")) { Write-Warning ".NET 8 SDK not listed by 'dotnet --list-sdks'; the build may fail." }

# ---- sculpt.dll ----
if (-not $SkipNative) {
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw "cl.exe not found: run this from the 'x64 Native Tools Command Prompt for VS 2022' (or pass -SkipNative)."
    }
    # The native DLL is always built in Release: OpenCascade is a Release /MD build.
    cmake -S (Join-Path $Root "Windows") -B $Native -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (did scripts\build_windows_deps.ps1 finish?)" }
    cmake --build $Native --parallel
    if ($LASTEXITCODE -ne 0) { throw "sculpt.dll build failed" }
}
$SculptDll = Join-Path $Native "appcore\sculpt.dll"
if (-not (Test-Path $SculptDll)) { throw "sculpt.dll not found at $SculptDll" }

function Copy-NativeDlls($dest) {
    Copy-Item $SculptDll $dest -Force
    # libEGL.dll, libGLESv2.dll and whatever else vcpkg's ANGLE needs (e.g. zlib1.dll).
    Get-ChildItem $Angle -Filter "*.dll" | Copy-Item -Destination $dest -Force
}

# ---- Sculpt3D.exe ----
dotnet build $Project -c $Configuration -p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw "dotnet build failed" }

$OutRoot = Join-Path $Root "Windows\Sculpt3D\bin\x64\$Configuration"
$Exe = Get-ChildItem -Recurse -File -Filter "Sculpt3D.exe" $OutRoot | Where-Object { $_.FullName -notmatch "\\publish\\" } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $Exe) { throw "Built app not found under $OutRoot" }
Copy-NativeDlls $Exe.DirectoryName
Write-Host "Built $($Exe.FullName)"

if ($Publish) {
    $Dist = Join-Path $Root "dist\Sculpt3D-win-x64"
    if (Test-Path $Dist) { Remove-Item -Recurse -Force $Dist }
    dotnet publish $Project -c Release -p:Platform=x64 -o $Dist
    if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed" }
    Copy-NativeDlls $Dist
    Write-Host "Published $Dist (self-contained: copy the folder, run Sculpt3D.exe)"
}

if ($Run) { & $Exe.FullName }
