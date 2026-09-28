execute_process(COMMAND "${BENCH_EXE}" --synthetic --batches 2 --iterations 1 --warmup 1
    --out "${TEST_DIR}/reference" RESULT_VARIABLE status OUTPUT_QUIET ERROR_VARIABLE error)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Synthetic benchmark failed: ${error}")
endif()
execute_process(COMMAND "${BENCH_EXE}" --synthetic --batches 2 --iterations 1 --warmup 1
    --reference "${TEST_DIR}/reference/report.json" --out "${TEST_DIR}/repeat"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_VARIABLE error)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Deterministic comparison failed: ${error}")
endif()
file(READ "${TEST_DIR}/repeat/report.json" repeat)
string(JSON matched GET "${repeat}" reference_equal)
if(NOT matched)
    message(FATAL_ERROR "Reference comparison was not recorded")
endif()
file(READ "${TEST_DIR}/reference/report.json" reference)
string(JSON changed SET "${reference}" semantics sample_height 0 "-999")
file(WRITE "${TEST_DIR}/mismatched.json" "${changed}")
execute_process(COMMAND "${BENCH_EXE}" --synthetic --batches 2 --iterations 1 --warmup 1
    --reference "${TEST_DIR}/mismatched.json" --out "${TEST_DIR}/must-not-match"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_VARIABLE error)
if(status EQUAL 0 OR NOT error MATCHES "Reference mismatch")
    message(FATAL_ERROR "Changed numerical results were not rejected: ${error}")
endif()
execute_process(COMMAND "${BENCH_EXE}" --synthetic --batches 0 --out "${TEST_DIR}/invalid"
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_VARIABLE error)
if(status EQUAL 0 OR NOT error MATCHES "counts must be")
    message(FATAL_ERROR "Invalid batch count was not rejected: ${error}")
endif()
