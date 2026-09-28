string(TOUPPER "${CMAKE_BUILD_TYPE}" bench_configuration)
set(bench_flags "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${bench_configuration}}")
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE bench_revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
configure_file(build_info.hpp.in build_info.hpp @ONLY)
add_executable(tatra-benchmark main.cpp)
target_include_directories(tatra-benchmark PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(tatra-benchmark PRIVATE TatraPeak::io nlohmann_json::nlohmann_json)
tatra_configure_target(tatra-benchmark)
if(TATRA_BUILD_TESTS)
    add_test(NAME benchmark.synthetic COMMAND ${CMAKE_COMMAND}
        "-DBENCH_EXE=$<TARGET_FILE:tatra-benchmark>"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/smoke"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/smoke_test.cmake")
    set_tests_properties(benchmark.synthetic PROPERTIES LABELS benchmark)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME benchmark.report_math COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_compare.py")
        set_tests_properties(benchmark.report_math PROPERTIES LABELS benchmark
            ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    endif()
endif()
