file(REMOVE_RECURSE "${TEST_DIR}")

execute_process(
    COMMAND "${TATRA_EXE}" generate-session --out "${TEST_DIR}/session"
        --heading-error-deg 0 --pose-delay-ns 0 --duration-ns 1000000000
    RESULT_VARIABLE generate_result
    OUTPUT_VARIABLE generate_output
    ERROR_VARIABLE generate_error)
if(NOT generate_result EQUAL 0)
    message(FATAL_ERROR "generate-session failed (${generate_result}): ${generate_error}")
endif()

foreach(run IN ITEMS first second)
    execute_process(
        COMMAND "${TATRA_EXE}" replay --dataset synthetic --session "${TEST_DIR}/session"
            --out "${TEST_DIR}/${run}"
        RESULT_VARIABLE replay_result
        OUTPUT_VARIABLE replay_output
        ERROR_VARIABLE replay_error)
    if(NOT replay_result EQUAL 0)
        message(FATAL_ERROR "replay ${run} failed (${replay_result}): ${replay_error}")
    endif()
endforeach()

file(READ "${TEST_DIR}/first/report.json" first_report)
file(READ "${TEST_DIR}/second/report.json" second_report)
if(NOT first_report STREQUAL second_report)
    message(FATAL_ERROR "Repeated replay reports are not byte-identical")
endif()

foreach(index_and_time IN ITEMS "000000-0" "000001-500000000" "000002-1000000000")
    set(relative "frames/${index_and_time}/result.json")
    if(NOT EXISTS "${TEST_DIR}/first/${relative}")
        message(FATAL_ERROR "Missing replay frame result: ${relative}")
    endif()
    file(READ "${TEST_DIR}/first/${relative}" first_frame)
    file(READ "${TEST_DIR}/second/${relative}" second_frame)
    if(NOT first_frame STREQUAL second_frame)
        message(FATAL_ERROR "Repeated replay frame differs: ${relative}")
    endif()
endforeach()

if(NOT first_report MATCHES "\"completed_count\": 3")
    message(FATAL_ERROR "Replay report does not contain three completed frames")
endif()
if(NOT first_report MATCHES "\"skipped_count\": 0")
    message(FATAL_ERROR "Replay report contains unexpected skipped frames")
endif()
