# Runs one call-chain scenario with "3X Frame Gen" on (`triple_frame_gen=1` in
# the ofxr_bridge.ini beside the layer DLL) and checks, from the flight log,
# that the session took three frames per application frame and handed them
# over as synthetic, synthetic, real.
#
# REQUIRE_SCENARIO_PASS: the scenario's own assertions must hold too. They
# count frames for a pair in most scenarios, so only one whose counts do not
# depend on the multiplier can be held to them.
foreach(required_variable IN ITEMS LAYER_DLL CALL_CHAIN TRIPLE_INI WORK_DIR MODE)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} was not provided")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(COPY "${LAYER_DLL}" DESTINATION "${WORK_DIR}")
file(COPY "${TRIPLE_INI}" DESTINATION "${WORK_DIR}")
get_filename_component(triple_ini_name "${TRIPLE_INI}" NAME)
file(RENAME "${WORK_DIR}/${triple_ini_name}" "${WORK_DIR}/ofxr_bridge.ini")

# With QUEUE_LAYER_DLL, the bridge's Vulkan queue layer is put in the test
# process's layer chain through the loader's explicit-layer path, and the
# call chain checks that it loaded.
if(DEFINED QUEUE_LAYER_DLL)
    file(COPY "${QUEUE_LAYER_DLL}" DESTINATION "${WORK_DIR}")
    get_filename_component(queue_layer_name "${QUEUE_LAYER_DLL}" NAME)
    file(TO_NATIVE_PATH "${WORK_DIR}/${queue_layer_name}" queue_layer_native)
    string(REPLACE "\\" "\\\\" queue_layer_json "${queue_layer_native}")
    file(WRITE "${WORK_DIR}/VK_LAYER_OFXR_queue_serialize.json"
"{
  \"file_format_version\": \"1.2.0\",
  \"layer\": {
    \"name\": \"VK_LAYER_OFXR_queue_serialize\",
    \"type\": \"GLOBAL\",
    \"library_path\": \"${queue_layer_json}\",
    \"api_version\": \"1.3.296\",
    \"implementation_version\": \"1\",
    \"description\": \"OFXR queue layer under test\",
    \"functions\": {
      \"vkNegotiateLoaderLayerInterfaceVersion\": \"OFXR_vkNegotiateLoaderLayerInterfaceVersion\",
      \"vkGetInstanceProcAddr\": \"OFXR_vkGetInstanceProcAddr\",
      \"vkGetDeviceProcAddr\": \"OFXR_vkGetDeviceProcAddr\"
    }
  }
}
")
    file(TO_NATIVE_PATH "${WORK_DIR}" work_dir_native)
    set(ENV{VK_LAYER_PATH} "${work_dir_native}")
    set(ENV{VK_INSTANCE_LAYERS} "VK_LAYER_OFXR_queue_serialize")
    set(ENV{OFXR_TEST_EXPECT_QUEUE_LAYER} "1")
endif()

get_filename_component(layer_name "${LAYER_DLL}" NAME)
execute_process(
    COMMAND "${CALL_CHAIN}"
        "${WORK_DIR}/${layer_name}"
        "${WORK_DIR}/fake-runtime.log"
        "${MODE}"
    RESULT_VARIABLE call_chain_result
    OUTPUT_VARIABLE call_chain_output
    ERROR_VARIABLE call_chain_error)
# A machine with no Vulkan implementation cannot run a Vulkan scenario at
# all; pass the call chain's marker through for SKIP_REGULAR_EXPRESSION.
string(FIND "${call_chain_error}" "SKIP: no Vulkan implementation" skipped)
if(NOT skipped EQUAL -1)
    message("${call_chain_error}")
    return()
endif()
if(REQUIRE_SCENARIO_PASS AND NOT call_chain_result EQUAL 0)
    message(FATAL_ERROR
        "Call chain '${MODE}' with 3X failed (${call_chain_result}):\n"
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

# presenter_transition 700: a is the frames per application frame.
string(FIND "${contents}"
    "op=presenter_transition result=700 dur_us=0 a=3 b=0 c=1" found)
if(found EQUAL -1)
    message(FATAL_ERROR "The session did not take three frames per application frame")
endif()

# One pair's synthesis per application frame, never two.
string(REGEX MATCHALL "phase=E op=synthesis_pair result=0" pairs "${contents}")
list(LENGTH pairs pair_count)
if(pair_count LESS 2)
    message(FATAL_ERROR "Expected at least two synthesised pairs, found ${pair_count}")
endif()

if(EXPECT_PRESENTER)
    # presenter_submission c: 2 a synthetic, 1 a real frame, 0 a repeat.
    string(REGEX MATCHALL
        "op=presenter_submission result=0 dur_us=0 a=[0-9]+ b=[0-9]+ c=[0-9]"
        submissions "${contents}")
    set(order "")
    foreach(submission IN LISTS submissions)
        string(REGEX REPLACE ".* c=([0-9])$" "\\1" half "${submission}")
        string(APPEND order "${half}")
    endforeach()
    string(FIND "${order}" "221221" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "The presenter did not hand over synthetic, synthetic, real: ${order}")
    endif()
else()
    # Inline: two internal cycles follow each pair's first hand-over.
    string(REGEX MATCHALL "phase=E op=internal_end_frame result=0" cycles "${contents}")
    list(LENGTH cycles cycle_count)
    math(EXPR expected_cycles "${pair_count} * 2")
    if(cycle_count LESS expected_cycles)
        message(FATAL_ERROR
            "Expected ${expected_cycles} inline cycles for ${pair_count} pairs, found ${cycle_count}")
    endif()
endif()
