# GeographicLib 2.7 unconditionally registers tests of its CLI tools.
# Those tools are excluded from our build; patch only that registration block.
# The pinned source archive is verified before this local, idempotent patch.
function(tatra_add_geographiclib)
    set(upstream_file "${geographiclib_SOURCE_DIR}/CMakeLists.txt")
    file(READ "${upstream_file}" content)
    set(original "enable_testing ()\nadd_subdirectory (tests)")
    set(replacement "# TatraPeak: dependency CLI tests disabled in the embedded build.")
    string(FIND "${content}" "${original}" original_position)
    string(FIND "${content}" "${replacement}" patched_position)
    if(NOT original_position EQUAL -1)
        string(REPLACE "${original}" "${replacement}" content "${content}")
        file(WRITE "${upstream_file}" "${content}")
    elseif(patched_position EQUAL -1)
        message(FATAL_ERROR "Unexpected GeographicLib CMake layout; review the dependency integration.")
    endif()
    add_subdirectory("${geographiclib_SOURCE_DIR}" "${geographiclib_BINARY_DIR}" EXCLUDE_FROM_ALL)
endfunction()
