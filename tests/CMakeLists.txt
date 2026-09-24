add_executable(tatra_tests unit/angles_test.cpp integration/scaffold_test.cpp)
target_link_libraries(tatra_tests PRIVATE TatraPeak::core GTest::gtest_main)
tatra_configure_target(tatra_tests)

include(GoogleTest)
gtest_discover_tests(tatra_tests DISCOVERY_MODE PRE_TEST)
add_test(NAME cli.help COMMAND tatra --help)
add_test(NAME cli.version COMMAND tatra --version)
add_test(NAME prepare.help COMMAND tatra-prepare --help)

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
