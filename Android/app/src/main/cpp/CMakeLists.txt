# Native side of the Android app: the shared app core (AppCore, with the GLES
# viewport) plus the JNI bridge in sculpt_jni.cpp.
cmake_minimum_required(VERSION 3.22)
project(Sculpt3DAndroid LANGUAGES C CXX)

get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
if(ANDROID_ABI STREQUAL "arm64-v8a")
  set(_slice android-arm64)
elseif(ANDROID_ABI STREQUAL "x86_64")
  set(_slice android-x64)
else()
  message(FATAL_ERROR "No OpenCascade slice for ${ANDROID_ABI}")
endif()
set(OCCT_INSTALL_DIR "${REPO}/third_party/occt-build/${_slice}-install" CACHE PATH "" FORCE)
set(SCULPT_WITH_GL ON CACHE BOOL "" FORCE)
set(SCULPT_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory("${REPO}/AppCore" appcore)

add_library(sculpt_jni SHARED sculpt_jni.cpp)
target_include_directories(sculpt_jni PRIVATE "${REPO}/AppCore/include")
target_link_libraries(sculpt_jni PRIVATE sculpt log)
