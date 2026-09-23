string(RANDOM LENGTH 12 token)
set(root "${CMAKE_CURRENT_BINARY_DIR}/record-import-${token}")
file(MAKE_DIRECTORY "${root}")
configure_file("${ABCD_DEPENDENCY}" "${root}/dependency.mjs" COPYONLY)
execute_process(COMMAND "${ABCD_FIXTURE}" --emit
  OUTPUT_FILE "${root}/record.mjs" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "record import emission failed")
endif()
execute_process(COMMAND "${ABCD_NODE}" "${ABCD_ORACLE}"
  "${root}/record.mjs" "${root}/dependency.mjs"
  RESULT_VARIABLE status ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "record import semantics failed: ${error}")
endif()
file(REMOVE_RECURSE "${root}")
