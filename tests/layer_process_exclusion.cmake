foreach(required_variable IN ITEMS LAYER_DLL CALL_CHAIN WORK_DIR)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} was not provided")
    endif()
endforeach()

# The call chain's own executable, in another case, in the exclusion list:
# the layer must refuse negotiation, which the call chain reports as a
# failure, and leave one record saying why.
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(COPY "${LAYER_DLL}" DESTINATION "${WORK_DIR}")
get_filename_component(call_chain_name "${CALL_CHAIN}" NAME)
string(TOUPPER "${call_chain_name}" call_chain_upper)
file(WRITE "${WORK_DIR}/ofxr_bridge.ini"
    "[ofxr]\nexcluded_processes=Other.exe;${call_chain_upper}\n"
    "[diagnostics]\nlogging_enabled=1\nmax_file_mb=1\nflush_each_event=1\n")

get_filename_component(layer_name "${LAYER_DLL}" NAME)
execute_process(
    COMMAND "${CALL_CHAIN}"
        "${WORK_DIR}/${layer_name}"
        "${WORK_DIR}/fake-runtime.log"
    RESULT_VARIABLE call_chain_result
    OUTPUT_VARIABLE call_chain_output
    ERROR_VARIABLE call_chain_error)
if(call_chain_result EQUAL 0)
    message(FATAL_ERROR
        "The layer negotiated inside an excluded process:\n"
        "${call_chain_output}\n${call_chain_error}")
endif()

file(GLOB flight_logs "${WORK_DIR}/ofxr-bridge-flight-*.log")
list(LENGTH flight_logs flight_log_count)
if(NOT flight_log_count EQUAL 1)
    message(FATAL_ERROR
        "Expected one OFXR flight log, found ${flight_log_count}")
endif()
list(GET flight_logs 0 flight_log)
file(READ "${flight_log}" contents)
string(FIND "${contents}" "phase=I op=process_excluded" found)
if(found EQUAL -1)
    message(FATAL_ERROR "Flight log is missing the process_excluded record")
endif()
string(FIND "${contents}" "op=instance_create" found)
if(NOT found EQUAL -1)
    message(FATAL_ERROR "The excluded process still created an instance through the layer")
endif()

# The same list without this executable: negotiation goes through.
file(WRITE "${WORK_DIR}/ofxr_bridge.ini"
    "[ofxr]\nexcluded_processes=Other.exe\n")
execute_process(
    COMMAND "${CALL_CHAIN}"
        "${WORK_DIR}/${layer_name}"
        "${WORK_DIR}/fake-runtime.log"
    RESULT_VARIABLE call_chain_result
    OUTPUT_VARIABLE call_chain_output
    ERROR_VARIABLE call_chain_error)
if(NOT call_chain_result EQUAL 0)
    message(FATAL_ERROR
        "Call chain failed with an exclusion list naming another process (${call_chain_result}):\n"
        "${call_chain_output}\n${call_chain_error}")
endif()

message(STATUS "OFXR bridge process exclusion verified")
