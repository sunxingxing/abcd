set(output "${CMAKE_CURRENT_BINARY_DIR}/guarded-global-call.mjs")
execute_process(COMMAND "${ABCD_FIXTURE}" "${ABCD_SAMPLE}" --emit
  OUTPUT_FILE "${output}" RESULT_VARIABLE emitted ERROR_VARIABLE error)
if(NOT emitted EQUAL 0)
  message(FATAL_ERROR "guarded call source emission failed: ${error}")
endif()
execute_process(COMMAND "${ABCD_NODE}" "${ABCD_ORACLE}" "${output}"
  RESULT_VARIABLE status ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "guarded call source oracle failed: ${error}")
endif()
