if(NOT DEFINED ABCD_STRING_TEST OR NOT DEFINED ABCD_NODE)
  message(FATAL_ERROR "String emitter executable and Node executable required")
endif()
set(output "${CMAKE_CURRENT_BINARY_DIR}/string-literal-roundtrip.js")
execute_process(COMMAND "${ABCD_STRING_TEST}" --javascript
  OUTPUT_FILE "${output}" RESULT_VARIABLE emitted)
if(NOT emitted EQUAL 0)
  message(FATAL_ERROR "String emission failed: ${emitted}")
endif()
execute_process(COMMAND "${ABCD_NODE}" "${output}"
  RESULT_VARIABLE evaluated OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT evaluated EQUAL 0)
  message(FATAL_ERROR "JS UTF-16 roundtrip failed: ${stdout}${stderr}")
endif()
