# Mutate a build-directory copy only. New group titles must never silently
# inherit another group's typed identity.
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(READ "${ISA_ROOT}/isa.yaml" original)
string(REPLACE "title: constant object loaders" "title: unclassified fixture"
       changed "${original}")
if(changed STREQUAL original)
  message(FATAL_ERROR "fixture did not change the expected ISA group")
endif()
file(WRITE "${OUTPUT_DIR}/isa.yaml" "${changed}")
execute_process(COMMAND "${RUBY}" "${ISA_ROOT}/gen.rb"
  -d "${OUTPUT_DIR}/isa.yaml" -r "${ISA_ROOT}/isapi.rb"
  -t "${TEMPLATE}" -o "${OUTPUT_DIR}/catalog.inc"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT error MATCHES "unclassified ISA groups: unclassified fixture")
  message(FATAL_ERROR "unknown group was not rejected: ${result}\n${output}\n${error}")
endif()
