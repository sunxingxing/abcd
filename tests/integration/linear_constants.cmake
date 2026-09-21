set(output "${CMAKE_CURRENT_BINARY_DIR}/linear-constants.mjs")
execute_process(COMMAND "${ABCD_CONSTANT_FIXTURE}" --emit OUTPUT_FILE "${output}"
  RESULT_VARIABLE emitted)
if(NOT emitted EQUAL 0)
  message(FATAL_ERROR "constant body emission failed")
endif()
execute_process(COMMAND "${ABCD_NODE}" "${ABCD_CONSTANT_ORACLE}" "${output}"
  RESULT_VARIABLE evaluated OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT evaluated EQUAL 0)
  message(FATAL_ERROR "constant JS oracle failed: ${stdout}${stderr}")
endif()
