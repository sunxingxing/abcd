string(RANDOM LENGTH 12 token)
set(root "${CMAKE_CURRENT_BINARY_DIR}/record-function-${token}")
file(MAKE_DIRECTORY "${root}")
execute_process(COMMAND "${ABCD_FIXTURE}" --emit
  OUTPUT_FILE "${root}/sync.mjs" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "synchronous record emission failed")
endif()
execute_process(COMMAND "${ABCD_FIXTURE}" --emit-async
  OUTPUT_FILE "${root}/async.mjs" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "async record emission failed")
endif()
execute_process(COMMAND "${ABCD_NODE}" "${ABCD_ORACLE}"
  "${root}/sync.mjs" "${root}/async.mjs"
  RESULT_VARIABLE status ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "record function semantics failed: ${error}")
endif()
file(REMOVE_RECURSE "${root}")
