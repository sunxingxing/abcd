set(root "${CMAKE_CURRENT_BINARY_DIR}/paired-record-import-class")
file(MAKE_DIRECTORY "${root}")
set(source "${root}/record-44106.partial.mjs")
execute_process(
  COMMAND "${ABCD_CLASS_FIXTURE}" "${ABCD_PAIRED_SAMPLE}" --record 44106
  OUTPUT_FILE "${source}" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "paired record source fixture failed: ${status}")
endif()
execute_process(COMMAND "${ABCD_NODE}" --check "${source}"
  RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "paired record source syntax failed: ${status}")
endif()
execute_process(
  COMMAND "${ABCD_NODE}" --experimental-vm-modules
    "${ABCD_RECORD_ORACLE}" "${source}"
  RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "paired record class oracle failed: ${status}")
endif()
