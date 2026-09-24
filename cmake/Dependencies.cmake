include(FetchContent)

# Eigen 3.4.0: header-only; avoid configuring its unrelated tools and tests.
FetchContent_Declare(eigen
    URL https://gitlab.com/libeigen/eigen/-/archive/3147391d946bb4b6c68edd901f2add6ac1f31f8c/eigen-3147391d946bb4b6c68edd901f2add6ac1f31f8c.tar.gz
    URL_HASH SHA256=0c8c490764f9c2a793133491adca0cd073b73e0bde965c68cbe58d91b5ed4261
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR tatra-header-only)
FetchContent_MakeAvailable(eigen)
add_library(tatra_eigen INTERFACE)
add_library(Eigen3::Eigen ALIAS tatra_eigen)
target_include_directories(tatra_eigen SYSTEM INTERFACE "${eigen_SOURCE_DIR}")

# GeographicLib 2.7; excluded tools are not part of the default build.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies")
set(BUILD_DOCUMENTATION OFF CACHE BOOL "No GeographicLib documentation")
set(BUILD_MANPAGES OFF CACHE BOOL "No GeographicLib manpages")
set(CONVERT_WARNINGS_TO_ERRORS OFF CACHE BOOL "Do not promote dependency warnings")
FetchContent_Declare(geographiclib
    URL https://codeload.github.com/geographiclib/geographiclib/tar.gz/15cddd7eed9f7f61ead6dba2229d45b271dbdeda
    URL_HASH SHA256=ca5da87cbd8d0f00f14b1bfb49fde37974aacc8c5320cfb27a62ef0e18417a64
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR tatra-deferred)
FetchContent_MakeAvailable(geographiclib)
include("${CMAKE_CURRENT_LIST_DIR}/GeographicLibIntegration.cmake")
tatra_add_geographiclib()

if(TATRA_BUILD_TESTS)
    set(INSTALL_GTEST OFF CACHE BOOL "Do not install GoogleTest")
    set(BUILD_GMOCK OFF CACHE BOOL "GoogleMock is not needed")
    set(gtest_force_shared_crt ON CACHE BOOL "Match the MSVC runtime")
    FetchContent_Declare(googletest
        URL https://codeload.github.com/google/googletest/tar.gz/52eb8108c5bdec04579160ae17225d66034bd723
        URL_HASH SHA256=745c55415660044610f7fcd3af7a6420d5de16a7dbb9ebfe2e131275676232be
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(googletest)
endif()

if(TATRA_BUILD_DESKTOP_IO)
    set(JSON_BuildTests OFF CACHE BOOL "No dependency tests")
    FetchContent_Declare(json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(json)
endif()
