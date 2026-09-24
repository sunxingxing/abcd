set(output "${CMAKE_CURRENT_BINARY_DIR}/historical-async-member.mjs")
execute_process(
  COMMAND "${ABCD_ASYNC_FIXTURE}" "${ABCD_ASYNC_SAMPLE}" --emit
  OUTPUT_FILE "${output}"
  RESULT_VARIABLE emitted)
if(NOT emitted EQUAL 0)
  message(FATAL_ERROR "historical async member emission failed")
endif()
execute_process(
  COMMAND "${ABCD_NODE}" "${ABCD_ASYNC_ORACLE}" "${output}"
  RESULT_VARIABLE evaluated OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT evaluated EQUAL 0)
  message(FATAL_ERROR "historical async member oracle failed: ${stdout}${stderr}")
endif()
