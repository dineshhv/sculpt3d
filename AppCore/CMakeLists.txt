# Sculpt3D shared app core: the geometry kernel (Core/), the document model,
# viewport and MCP server, behind the C API in include/sculpt/sculpt_c.h.
#
# Used by Android (Android/app/src/main/cpp/CMakeLists.txt adds this directory)
# and Windows (Windows/CMakeLists.txt). On macOS it builds the model and its
# tests against third_party/occt-build/macos-install, with the GL viewport
# compiled out (SCULPT_WITH_GL=OFF), for fast iteration.
#
# Inputs:
#   OCCT_INSTALL_DIR  OpenCascade install prefix (include/opencascade + lib*/libTK*)
#   SCULPT_WITH_GL    build the OpenGL ES 3 viewport (default ON off Apple)
cmake_minimum_required(VERSION 3.22)
project(SculptCore LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(SCULPT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

if(APPLE)
  set(_gl_default OFF)
else()
  set(_gl_default ON)
endif()
option(SCULPT_WITH_GL "Build the OpenGL ES 3 viewport" ${_gl_default})
option(SCULPT_TESTS "Build the core tests" ${APPLE})

if(NOT OCCT_INSTALL_DIR)
  if(APPLE)
    set(OCCT_INSTALL_DIR "${SCULPT_ROOT}/third_party/occt-build/macos-install")
  endif()
endif()
# Unix install layout (include/opencascade, lib) everywhere; OCCT's Windows
# layout (inc, win64/vc14/lib) is accepted too.
if(EXISTS "${OCCT_INSTALL_DIR}/include/opencascade")
  set(OCCT_INCLUDE_DIR "${OCCT_INSTALL_DIR}/include/opencascade")
elseif(EXISTS "${OCCT_INSTALL_DIR}/inc/Standard.hxx")
  set(OCCT_INCLUDE_DIR "${OCCT_INSTALL_DIR}/inc")
else()
  message(FATAL_ERROR "Set OCCT_INSTALL_DIR to an OpenCascade install (scripts/build_occt.sh); got '${OCCT_INSTALL_DIR}'")
endif()

# OpenCascade toolkits, in link order (dependents first).
set(OCCT_TOOLKITS
  TKDESTEP TKDEIGES TKDESTL TKDEOBJ TKDEGLTF TKDEVRML TKDEPLY TKDE TKXSDRAW
  TKXCAF TKVCAF TKCAF TKStd TKStdL TKBin TKBinL TKXml TKXmlL TKLCAF TKCDF
  TKV3d TKService TKHLR TKMesh TKOffset TKFillet TKFeat TKBool TKBO TKPrim
  TKShHealing TKTopAlgo TKGeomAlgo TKBRep TKGeomBase TKG3d TKG2d TKMath TKXSBase
  TKRWMesh TKernel)
set(OCCT_LIBS "")
foreach(tk IN LISTS OCCT_TOOLKITS)
  find_library(OCCT_${tk} NAMES ${tk} PATHS "${OCCT_INSTALL_DIR}"
               PATH_SUFFIXES lib lib/static libs/${ANDROID_ABI} win64/vc14/lib win64/vc14/libd
               NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
  if(OCCT_${tk})
    list(APPEND OCCT_LIBS "${OCCT_${tk}}")
  endif()
endforeach()
if(NOT OCCT_LIBS)
  message(FATAL_ERROR "No OpenCascade libraries found under ${OCCT_INSTALL_DIR}")
endif()

file(GLOB KERNEL_SOURCES CONFIGURE_DEPENDS "${SCULPT_ROOT}/Core/src/*.cpp")
file(GLOB_RECURSE MODEL_SOURCES CONFIGURE_DEPENDS src/model/*.cpp)
file(GLOB_RECURSE MCP_SOURCES CONFIGURE_DEPENDS src/mcp/*.cpp)
file(GLOB_RECURSE API_SOURCES CONFIGURE_DEPENDS src/api/*.cpp)
file(GLOB_RECURSE VIEWPORT_SOURCES CONFIGURE_DEPENDS src/viewport/*.cpp)

add_library(sculpt_core STATIC ${KERNEL_SOURCES} ${MODEL_SOURCES} ${MCP_SOURCES})
target_include_directories(sculpt_core PUBLIC
  "${SCULPT_ROOT}/Core/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${SCULPT_ROOT}/third_party/nlohmann/.."
  "${OCCT_INCLUDE_DIR}")
target_link_libraries(sculpt_core PUBLIC ${OCCT_LIBS})
if(WIN32)
  # OCCT_STATIC_BUILD (HAVE_NO_DLL before OCCT 7.7): OpenCascade is linked as
  # static libraries, so its headers must not declare __declspec(dllimport).
  target_compile_definitions(sculpt_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES
    OCCT_STATIC_BUILD HAVE_NO_DLL _CRT_SECURE_NO_WARNINGS)
  if(MSVC)
    target_compile_options(sculpt_core PRIVATE /bigobj /utf-8 /Zc:__cplusplus)
  endif()
  # System libraries the static OpenCascade toolkits use (OSD, Image_AlienPixMap via WIC).
  target_link_libraries(sculpt_core PUBLIC ws2_32 user32 advapi32 gdi32 shell32 ole32 psapi windowscodecs)
elseif(ANDROID)
  target_link_libraries(sculpt_core PUBLIC log)
else()
  target_compile_options(sculpt_core PRIVATE -Wno-deprecated-declarations)
endif()
find_package(Threads REQUIRED)
target_link_libraries(sculpt_core PUBLIC Threads::Threads)

# The shared library front ends load: C API + viewport.
if(SCULPT_WITH_GL)
  add_library(sculpt SHARED ${API_SOURCES} ${VIEWPORT_SOURCES})
  target_compile_definitions(sculpt PRIVATE SCULPT_BUILD_DLL SCULPT_WITH_GL=1)
  target_link_libraries(sculpt PRIVATE sculpt_core)
  if(ANDROID)
    target_link_libraries(sculpt PRIVATE GLESv3 EGL android)
  elseif(WIN32)
    # ANGLE provides GLES 3 + EGL on Windows; Windows/CMakeLists.txt sets ANGLE_DIR.
    target_include_directories(sculpt PRIVATE "${ANGLE_DIR}/include")
    target_link_libraries(sculpt PRIVATE "${ANGLE_DIR}/lib/libGLESv2.lib" "${ANGLE_DIR}/lib/libEGL.lib")
    if(MSVC)
      target_compile_options(sculpt PRIVATE /utf-8 /bigobj /Zc:__cplusplus)
    endif()
  endif()
elseif(API_SOURCES)
  # Headless C API for tests on macOS: the viewport controller still does
  # camera, picking, input and overlays (GL-free sources); only rendering is out.
  add_library(sculpt_headless STATIC ${API_SOURCES}
    src/viewport/Camera.cpp src/viewport/Picking.cpp src/viewport/Overlays.cpp)
  target_compile_definitions(sculpt_headless PRIVATE SCULPT_WITH_GL=0)
  target_link_libraries(sculpt_headless PUBLIC sculpt_core)
endif()

if(SCULPT_TESTS)
  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
  foreach(t IN LISTS TEST_SOURCES)
    get_filename_component(name "${t}" NAME_WE)
    add_executable(${name} "${t}")
    target_compile_definitions(${name} PRIVATE SCULPT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    if(TARGET sculpt_headless)
      target_link_libraries(${name} PRIVATE sculpt_headless)
    else()
      target_link_libraries(${name} PRIVATE sculpt_core)
    endif()
    if(APPLE)
      target_link_libraries(${name} PRIVATE "-framework Foundation" "-framework AppKit" "-framework IOKit")
    endif()
  endforeach()
endif()
