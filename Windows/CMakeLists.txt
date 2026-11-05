# Native part of the Windows app: sculpt.dll (AppCore + GLES viewport over ANGLE,
# plus the SwapChainPanel host in native/AngleHost.cpp).
# Built by scripts/build_windows.ps1; the WinUI app (Windows/Sculpt3D) loads it.
#
#   cmake -S Windows -B build/windows-native -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/windows-native
#
# Run from a "x64 Native Tools Command Prompt for VS 2022" (MSVC x64 on PATH).
cmake_minimum_required(VERSION 3.22)
project(Sculpt3DWindowsNative LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Build sculpt.dll for x64 (use the x64 Native Tools Command Prompt for VS 2022)")
endif()

get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
# /MD: the same C runtime as OpenCascade (built with MultiThreadedDLL).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
set(OCCT_INSTALL_DIR "${REPO}/third_party/occt-build/windows-x64-install" CACHE PATH "OpenCascade install")
set(ANGLE_DIR "${REPO}/third_party/vcpkg/installed/x64-windows" CACHE PATH "ANGLE (vcpkg) prefix")
set(SCULPT_WITH_GL ON CACHE BOOL "" FORCE)
set(SCULPT_TESTS OFF CACHE BOOL "" FORCE)

if(NOT EXISTS "${ANGLE_DIR}/include/EGL/egl.h" OR NOT EXISTS "${ANGLE_DIR}/lib/libGLESv2.lib")
  message(FATAL_ERROR "ANGLE not found under ${ANGLE_DIR}: run scripts\\build_windows_deps.ps1")
endif()

add_subdirectory("${REPO}/AppCore" appcore)

# The SwapChainPanel host (Windows only; AppCore itself stays platform-neutral).
target_sources(sculpt PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/native/AngleHost.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/native/sculpt_win.h")
target_include_directories(sculpt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/native")
target_link_libraries(sculpt PRIVATE d3d11 dxgi)
