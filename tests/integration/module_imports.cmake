set(root "${CMAKE_CURRENT_BINARY_DIR}/module-imports")
file(MAKE_DIRECTORY "${root}")
set(source "${root}/recovered.mjs")
execute_process(COMMAND "${ABCD_IMPORT_FIXTURE}" --emit
  OUTPUT_FILE "${source}" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "module import fixture failed: ${status}")
endif()
file(COPY "${ABCD_IMPORT_DEPENDENCY}" DESTINATION "${root}")
execute_process(COMMAND "${ABCD_NODE}" "${ABCD_IMPORT_ORACLE}" "${source}"
  RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "module import runtime oracle failed: ${status}")
endif()
