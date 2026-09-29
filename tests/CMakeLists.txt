add_executable(tatra_tests unit/angles_test.cpp unit/catalog_test.cpp unit/terrain_test.cpp unit/pose_timeline_test.cpp
    unit/reprojection_metrics_test.cpp integration/scaffold_test.cpp)
target_link_libraries(tatra_tests PRIVATE TatraPeak::core TatraPeak::evaluation GTest::gtest_main)
tatra_configure_target(tatra_tests)

include(GoogleTest)
gtest_discover_tests(tatra_tests DISCOVERY_MODE PRE_TEST)
if(TATRA_BUILD_CLI)
    add_test(NAME cli.help COMMAND tatra --help)
    add_test(NAME cli.version COMMAND tatra --version)
    add_test(NAME prepare.help COMMAND tatra-prepare --help)
endif()

if(TATRA_BUILD_MOBILE_BRIDGE)
    add_executable(tatra_mobile_c_test integration/mobile_c_api_test.c)
    target_link_libraries(tatra_mobile_c_test PRIVATE TatraPeak::mobile)
    tatra_configure_target(tatra_mobile_c_test)
    add_test(NAME mobile.c_api COMMAND tatra_mobile_c_test)
endif()

if(TATRA_BUILD_DESKTOP_IO)
    add_executable(tatra_io_tests integration/desktop_io_test.cpp)
    target_link_libraries(tatra_io_tests PRIVATE TatraPeak::io GTest::gtest_main
        nlohmann_json::nlohmann_json)
    target_compile_definitions(tatra_io_tests PRIVATE
        TATRA_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/t08")
    tatra_configure_target(tatra_io_tests)
    gtest_discover_tests(tatra_io_tests DISCOVERY_MODE PRE_TEST)

    add_executable(tatra_dem_tests integration/geotiff_test.cpp)
    target_link_libraries(tatra_dem_tests PRIVATE TatraPeak::io GTest::gtest_main GDAL::GDAL)
    tatra_configure_target(tatra_dem_tests)
    gtest_discover_tests(tatra_dem_tests DISCOVERY_MODE PRE_TEST)

    add_executable(tatra_peak_import_tests integration/osm_peak_importer_test.cpp)
    target_link_libraries(tatra_peak_import_tests PRIVATE TatraPeak::io GTest::gtest_main
        nlohmann_json::nlohmann_json)
    tatra_configure_target(tatra_peak_import_tests)
    gtest_discover_tests(tatra_peak_import_tests DISCOVERY_MODE PRE_TEST)

    add_executable(tatra_dataset_tests integration/dataset_loader_test.cpp)
    target_link_libraries(tatra_dataset_tests PRIVATE TatraPeak::io GTest::gtest_main
        GDAL::GDAL nlohmann_json::nlohmann_json)
    target_include_directories(tatra_dataset_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/adapters/desktop")
    tatra_configure_target(tatra_dataset_tests)
    gtest_discover_tests(tatra_dataset_tests DISCOVERY_MODE PRE_TEST)

    add_executable(tatra_session_tests integration/session_loader_test.cpp)
    target_link_libraries(tatra_session_tests PRIVATE TatraPeak::io GTest::gtest_main
        nlohmann_json::nlohmann_json)
    tatra_configure_target(tatra_session_tests)
    gtest_discover_tests(tatra_session_tests DISCOVERY_MODE PRE_TEST)

    if(TATRA_BUILD_CLI)
        add_test(NAME cli.render.synthetic COMMAND ${CMAKE_COMMAND}
            "-DTATRA_EXE=$<TARGET_FILE:tatra>"
            "-DSCENE=${PROJECT_SOURCE_DIR}/examples/synthetic-scene.json"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-render"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/integration/cli_render_test.cmake")
        add_test(NAME cli.replay.synthetic COMMAND ${CMAKE_COMMAND}
            "-DTATRA_EXE=$<TARGET_FILE:tatra>"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-replay"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/integration/cli_replay_test.cmake")
        if(TATRA_REAL_DATASET_DIR)
            add_test(NAME cli.render.tatras-v1 COMMAND ${CMAKE_COMMAND}
                "-DTATRA_EXE=$<TARGET_FILE:tatra>"
                "-DDATASET=${TATRA_REAL_DATASET_DIR}"
                "-DSCENE_DIR=${PROJECT_SOURCE_DIR}/examples/tatras-v1"
                "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/t14-real-data"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/integration/t14_real_dataset_test.cmake")
            set_tests_properties(cli.render.tatras-v1 PROPERTIES LABELS real-data)
        endif()
    endif()
endif()

# Compile each public header in isolation to catch hidden include dependencies.
set(public_headers angles types geo camera terrain catalog visibility engine
    validation pose_timeline desktop/io evaluation/reprojection_metrics)
set(header_sources "")
foreach(header IN LISTS public_headers)
    string(REPLACE "/" "_" stem "${header}")
    set(source "${CMAKE_CURRENT_BINARY_DIR}/header_${stem}.cpp")
    file(WRITE "${source}" "#include <tatra/${header}.hpp>\n")
    list(APPEND header_sources "${source}")
endforeach()
add_library(tatra_header_checks OBJECT ${header_sources})
target_link_libraries(tatra_header_checks PRIVATE TatraPeak::core)
tatra_configure_target(tatra_header_checks)

if(TATRA_BUILD_MOBILE_BRIDGE)
    set(mobile_header_source "${CMAKE_CURRENT_BINARY_DIR}/header_mobile.c")
    file(WRITE "${mobile_header_source}" "#include <tatra/mobile.h>\n")
    add_library(tatra_mobile_header_check OBJECT "${mobile_header_source}")
    target_link_libraries(tatra_mobile_header_check PRIVATE TatraPeak::mobile)
    tatra_configure_target(tatra_mobile_header_check)
endif()
