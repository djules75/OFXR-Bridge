# Each run gets a new empty physical application directory. The provider
# arrives only after NGX has cached FeatureNotFound; earlier evidence is kept.
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(dir "${WORK_ROOT}/${run_id}")
file(MAKE_DIRECTORY "${dir}")
file(COPY "${TEST_EXE}" "${FORWARDER}" DESTINATION "${dir}")
get_filename_component(exe "${TEST_EXE}" NAME)
execute_process(COMMAND "${dir}/${exe}" native-dlssg-late "${PROVIDER}"
    WORKING_DIRECTORY "${dir}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
    TIMEOUT 120)
file(WRITE "${dir}/result.log" "${output}\n${error}")
message("${output}\n${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Late native FG GPU regression failed: ${result}")
endif()
