set(output "${CMAKE_CURRENT_BINARY_DIR}/scalar-expressions.mjs")
execute_process(COMMAND "${ABCD_SCALAR_FIXTURE}" OUTPUT_FILE "${output}"
  RESULT_VARIABLE emitted)
if(NOT emitted EQUAL 0)
  message(FATAL_ERROR "scalar expression emission failed")
endif()
execute_process(COMMAND "${ABCD_NODE}" "${ABCD_SCALAR_ORACLE}" "${output}"
  RESULT_VARIABLE evaluated OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT evaluated EQUAL 0)
  message(FATAL_ERROR "scalar JS oracle failed: ${stdout}${stderr}")
endif()
