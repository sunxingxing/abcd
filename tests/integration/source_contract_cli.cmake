# Source target/usage checks include filesystem non-effects and exact typed errors.
execute_process(COMMAND "${ABCD_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/source_contract_cli.py"
  "${ABCD_STRUCTURE}" "${ABCD_SEMANTIC}"
  "${CMAKE_CURRENT_LIST_DIR}/../fixtures/isa/object_methods/methods-O0.abc"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "source contract CLI: ${output}${error}")
endif()
