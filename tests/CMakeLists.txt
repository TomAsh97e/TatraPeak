add_executable(tatra_tests unit/angles_test.cpp integration/scaffold_test.cpp)
target_link_libraries(tatra_tests PRIVATE TatraPeak::core GTest::gtest_main)
tatra_configure_target(tatra_tests)

include(GoogleTest)
gtest_discover_tests(tatra_tests DISCOVERY_MODE PRE_TEST)
add_test(NAME cli.help COMMAND tatra --help)
add_test(NAME cli.version COMMAND tatra --version)
add_test(NAME prepare.help COMMAND tatra-prepare --help)

if(TATRA_BUILD_DESKTOP_IO)
    add_executable(tatra_io_tests integration/desktop_io_test.cpp)
    target_link_libraries(tatra_io_tests PRIVATE TatraPeak::io GTest::gtest_main
        nlohmann_json::nlohmann_json)
    target_compile_definitions(tatra_io_tests PRIVATE
        TATRA_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/t08")
    tatra_configure_target(tatra_io_tests)
    gtest_discover_tests(tatra_io_tests DISCOVERY_MODE PRE_TEST)
    add_test(NAME cli.render.synthetic COMMAND ${CMAKE_COMMAND}
        "-DTATRA_EXE=$<TARGET_FILE:tatra>"
        "-DSCENE=${PROJECT_SOURCE_DIR}/examples/synthetic-scene.json"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-render"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/integration/cli_render_test.cmake")
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
