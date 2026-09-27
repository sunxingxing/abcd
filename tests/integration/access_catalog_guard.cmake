# Mutate only an isolated copy, never the pinned upstream ISA. An unknown
# access mnemonic must fail catalog generation instead of silently disappearing.
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(READ "${ISA_ROOT}/isa.yaml" original)
string(REPLACE "sig: ldundefined" "sig: ldunclassifiedfixture" changed "${original}")
if(changed STREQUAL original)
  message(FATAL_ERROR "fixture did not change the expected ISA declaration")
endif()
file(WRITE "${OUTPUT_DIR}/isa.yaml" "${changed}")
execute_process(COMMAND "${RUBY}" "${ISA_ROOT}/gen.rb"
  -d "${OUTPUT_DIR}/isa.yaml" -r "${ISA_ROOT}/isapi.rb"
  -t "${TEMPLATE}" -o "${OUTPUT_DIR}/access.inc"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT error MATCHES "unclassified access mnemonic ldunclassifiedfixture")
  message(FATAL_ERROR "missing catalog entry was not rejected: ${result}\n${output}\n${error}")
endif()
