file(MAKE_DIRECTORY "${DESTINATION}")
file(COPY_FILE "${TEST_EXE}" "${DESTINATION}/${GAME_EXE}" ONLY_IF_DIFFERENT)
file(COPY_FILE "${LAYER_DLL}" "${DESTINATION}/XR_APILAYER_XRFrameBridge_diagnostic.dll" ONLY_IF_DIFFERENT)
if(MODE STREQUAL "selected")
  file(COPY_FILE "${LUKE_FIXTURE}" "${DESTINATION}/luke-runtime-fixture.dll" ONLY_IF_DIFFERENT)
endif()
execute_process(COMMAND "${DESTINATION}/${GAME_EXE}"
  "${DESTINATION}/XR_APILAYER_XRFrameBridge_diagnostic.dll" "${MODE}"
  WORKING_DIRECTORY "${DESTINATION}" RESULT_VARIABLE result TIMEOUT 20)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Selected shipping-runtime early bootstrap failed: ${result}")
endif()
