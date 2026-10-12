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
# Structural failures and records without a source status carry no metadata:
# they are module_unknown with their own counters, never "no module".
file(WRITE "${root}/synthetic/manifest.json" [=[
{"schema":2,"view":"declarations_with_value_source","abc_version":[24,0,0,0],"records":[
{"entity":1,"structure_failure":{"domain":"abcd::structure::StructureFault","value":2,"entity":1}},
{"entity":2,"structure_failure":{"domain":"abcd::structure::PathError","value":2}},
{"entity":3}]}
]=])
execute_process(COMMAND "${ABCD_CAPABILITY}" "${root}/synthetic"
  RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "synthetic capability report failed: ${error}")
endif()
foreach(key IN ITEMS records non_module module_unknown structure_failed unknown_status failed)
  string(JSON ${key} GET "${report}" total ${key})
endforeach()
string(JSON leaf GET "${report}" pareto 0 leaf)
if(NOT records EQUAL 3 OR NOT non_module EQUAL 0 OR NOT module_unknown EQUAL 3
    OR NOT structure_failed EQUAL 2 OR NOT unknown_status EQUAL 1 OR NOT failed EQUAL 0
    OR NOT leaf MATCHES "^structure:abcd::structure::")
  message(FATAL_ERROR "structure failures misclassified: ${report}")
endif()
file(REMOVE_RECURSE "${root}")
