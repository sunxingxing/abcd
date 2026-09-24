string(RANDOM LENGTH 12 token)
set(output "${CMAKE_CURRENT_BINARY_DIR}/constructed-${token}.mjs")
execute_process(COMMAND "${ABCD_FIXTURE}" --emit-constructed
  OUTPUT_FILE "${output}" RESULT_VARIABLE emitted)
if(NOT emitted EQUAL 0)
  message(FATAL_ERROR "record constructor fixture failed")
endif()
execute_process(COMMAND "${ABCD_NODE}" "${ABCD_ORACLE}" "${output}"
  RESULT_VARIABLE status ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "record constructor oracle failed: ${error}")
endif()
file(REMOVE "${output}")
