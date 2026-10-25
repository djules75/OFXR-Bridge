# Exercise shipping process-name routing without spoofing a production override.
if(NOT EXISTS "${TEST_EXE}" OR NOT TEST_DIR)
    message(FATAL_ERROR "Missing isolated origin test executable/directory")
endif()
file(MAKE_DIRECTORY "${TEST_DIR}")
configure_file("${TEST_EXE}" "${TEST_DIR}/Outlaws.exe" COPYONLY)
execute_process(COMMAND "${TEST_DIR}/Outlaws.exe" outlaws-protected
    WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
message("${output}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Protected Outlaws origin failed (${result}): ${error}")
endif()
