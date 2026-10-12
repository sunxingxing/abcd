# abc_capability over a real export: denominators are self-consistent and a
# missing export counts as an export failure, never as zero records.
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef nonce)
set(root "${CMAKE_CURRENT_BINARY_DIR}/capability_cli_${nonce}")
execute_process(COMMAND "${ABCD_STRUCTURE}" "${ABCD_SAMPLE}" "${root}/export" --value-source
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "export failed: ${error}")
endif()
execute_process(COMMAND "${ABCD_STRUCTURE}" "${ABCD_SAMPLE}" "${root}/structure_only"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "structure-only export failed: ${error}")
endif()
execute_process(COMMAND "${ABCD_CAPABILITY}" "${root}/export" "${root}/missing" "${root}/structure_only"
  RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "capability report failed: ${error}")
endif()
foreach(key IN ITEMS records non_module module_unknown module_records complete incomplete failed
    profile_refused module_context_failed)
  string(JSON ${key} GET "${report}" total ${key})
endforeach()
string(JSON exports GET "${report}" exports)
string(JSON export_failures GET "${report}" export_failures)
string(JSON view_mismatch GET "${report}" view_mismatch)
math(EXPR modules "${records} - ${non_module} - ${module_unknown}")
math(EXPR outcomes "${complete} + ${incomplete} + ${failed} + ${profile_refused} + ${module_context_failed}")
# A structure-only export carries no recovery outcomes: it is a view mismatch.
if(NOT exports EQUAL 3 OR NOT export_failures EQUAL 1 OR NOT view_mismatch EQUAL 1
    OR NOT module_unknown EQUAL 0 OR NOT module_records EQUAL modules
    OR NOT outcomes EQUAL module_records OR records EQUAL 0 OR complete EQUAL 0)
  message(FATAL_ERROR "inconsistent capability report: ${report}")
endif()
file(READ "${root}/export/manifest.json" manifest)
string(JSON manifest_records LENGTH "${manifest}" records)
if(NOT records EQUAL manifest_records)
  message(FATAL_ERROR "report lost records: ${records} vs ${manifest_records}")
endif()
file(REMOVE_RECURSE "${root}")
