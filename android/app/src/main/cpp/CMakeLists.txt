cmake_minimum_required(VERSION 3.28)
project(TatraPeakJni LANGUAGES C CXX)

set(TATRA_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(TATRA_BUILD_DESKTOP_IO OFF CACHE BOOL "" FORCE)
set(TATRA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TATRA_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(TATRA_BUILD_MOBILE_BRIDGE ON CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../../../.." tatra EXCLUDE_FROM_ALL)

add_library(tatra_jni SHARED tatra_jni.cpp)
target_link_libraries(tatra_jni PRIVATE TatraPeak::mobile)
target_compile_features(tatra_jni PRIVATE cxx_std_20)
target_compile_options(tatra_jni PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(tatra_jni PROPERTIES CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
if(NOT ANDROID)
    # The same JNI adapter runs under Linux JVM tests, without Android stubs.
    find_package(JNI REQUIRED)
    target_include_directories(tatra_jni PRIVATE ${JNI_INCLUDE_DIRS})
endif()
