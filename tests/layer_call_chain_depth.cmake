# Runs one call-chain scenario at the pipeline depth PIPELINE_INI sets. The
# layer reads `deep_pipeline` from the ofxr_bridge.ini beside its DLL, and the
# shipped ini turns it on, so the other depth runs from a scratch directory
# with an ini of its own.
foreach(required_variable IN ITEMS LAYER_DLL CALL_CHAIN PIPELINE_INI WORK_DIR MODE)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} was not provided")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(COPY "${LAYER_DLL}" DESTINATION "${WORK_DIR}")
file(COPY "${PIPELINE_INI}" DESTINATION "${WORK_DIR}")
get_filename_component(pipeline_ini_name "${PIPELINE_INI}" NAME)
file(RENAME "${WORK_DIR}/${pipeline_ini_name}" "${WORK_DIR}/ofxr_bridge.ini")

get_filename_component(layer_name "${LAYER_DLL}" NAME)
set(mode_arguments "${MODE}")
if(MODE STREQUAL "default")
    set(mode_arguments "")
endif()
execute_process(
    COMMAND "${CALL_CHAIN}"
        "${WORK_DIR}/${layer_name}"
        "${WORK_DIR}/fake-runtime.log"
        ${mode_arguments}
    RESULT_VARIABLE call_chain_result
    OUTPUT_VARIABLE call_chain_output
    ERROR_VARIABLE call_chain_error)
if(NOT call_chain_result EQUAL 0)
    message(FATAL_ERROR
        "Call chain '${MODE}' at the other pipeline depth failed (${call_chain_result}):\n"
        "${call_chain_output}\n${call_chain_error}")
endif()
