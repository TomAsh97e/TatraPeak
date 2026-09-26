cmake_minimum_required(VERSION 3.28)
project(TatraPeak VERSION 0.1.0 LANGUAGES CXX)

option(TATRA_BUILD_TESTS "Build TatraPeak tests" ON)
option(TATRA_BUILD_DESKTOP_IO "Build desktop adapters (requires GDAL)" OFF)
option(TATRA_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
set(TATRA_REAL_DATASET_DIR "" CACHE PATH "Optional validated dataset used by real-data tests")
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(TATRA_BUILD_TESTS)
    enable_testing()
endif()

include(cmake/ProjectOptions.cmake)
include(cmake/Dependencies.cmake)
configure_file(include/tatra/version.hpp.in generated/tatra/version.hpp @ONLY)

add_library(tatra_core STATIC
    src/angles.cpp src/validation.cpp src/geo.cpp src/camera.cpp
    src/terrain.cpp src/catalog.cpp src/visibility.cpp
    src/engine.cpp src/pose_timeline.cpp)
add_library(TatraPeak::core ALIAS tatra_core)
target_compile_features(tatra_core PUBLIC cxx_std_20)
target_include_directories(tatra_core PUBLIC
    "${PROJECT_SOURCE_DIR}/include" "${PROJECT_BINARY_DIR}/generated")
target_link_libraries(tatra_core PUBLIC Eigen3::Eigen PRIVATE GeographicLib)
tatra_configure_target(tatra_core)

add_library(tatra_evaluation STATIC tools/evaluation/reprojection_metrics.cpp)
add_library(TatraPeak::evaluation ALIAS tatra_evaluation)
target_link_libraries(tatra_evaluation PUBLIC TatraPeak::core)
tatra_configure_target(tatra_evaluation)

if(TATRA_BUILD_DESKTOP_IO)
    find_package(GDAL REQUIRED)
    add_library(tatra_io STATIC
        adapters/desktop/dataset_loader.cpp adapters/desktop/geotiff_loader.cpp
        adapters/desktop/scene_loader.cpp adapters/desktop/session_loader.cpp
        adapters/desktop/result_writer.cpp adapters/desktop/svg_writer.cpp
        adapters/desktop/data_preparation.cpp adapters/desktop/file_hash.cpp
        adapters/desktop/osm_peak_importer.cpp adapters/desktop/manifest_parser.cpp)
    add_library(TatraPeak::io ALIAS tatra_io)
    target_link_libraries(tatra_io PUBLIC TatraPeak::core
        PRIVATE GDAL::GDAL GeographicLib nlohmann_json::nlohmann_json)
    tatra_configure_target(tatra_io)
endif()

add_executable(tatra apps/tatra/main.cpp)
add_executable(tatra-prepare apps/prepare/main.cpp)
foreach(app IN ITEMS tatra tatra-prepare)
    target_link_libraries(${app} PRIVATE TatraPeak::core)
    if(TATRA_BUILD_DESKTOP_IO)
        target_link_libraries(${app} PRIVATE TatraPeak::io)
    endif()
    tatra_configure_target(${app})
endforeach()
target_link_libraries(tatra PRIVATE TatraPeak::evaluation)
target_compile_definitions(tatra PRIVATE TATRA_BUILD_DESKTOP_IO=$<BOOL:${TATRA_BUILD_DESKTOP_IO}>)
target_compile_definitions(tatra-prepare PRIVATE
    TATRA_BUILD_DESKTOP_IO=$<BOOL:${TATRA_BUILD_DESKTOP_IO}>)

if(TATRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
