# Runs one call-chain scenario with the deeper pipeline on. The layer reads
# `deep_pipeline` from the ofxr_bridge.ini beside its DLL, and the shipped ini
# leaves it off, so the scenario runs from a scratch directory with its own.
foreach(required_variable IN ITEMS LAYER_DLL CALL_CHAIN DEEP_INI WORK_DIR MODE)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} was not provided")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(COPY "${LAYER_DLL}" DESTINATION "${WORK_DIR}")
file(COPY "${DEEP_INI}" DESTINATION "${WORK_DIR}")
get_filename_component(deep_ini_name "${DEEP_INI}" NAME)
file(RENAME "${WORK_DIR}/${deep_ini_name}" "${WORK_DIR}/ofxr_bridge.ini")

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
        "Deep-pipeline call chain '${MODE}' failed (${call_chain_result}):\n"
        "${call_chain_output}\n${call_chain_error}")
endif()
