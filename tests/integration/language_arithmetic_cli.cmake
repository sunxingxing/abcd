# Option selection must precede file access; numeric policy requires the common route.
foreach(options IN ITEMS
    "--language-arithmetic" "--language-arithmetic;--language-arithmetic"
    "--language-arithmetic;--instructions" "--instructions;--language-arithmetic"
    "--language-arithmetic;--value-source;--value-source"
    "--value-source;--language-arithmetic;--language-arithmetic"
    "--language-arithmetic;--conditional-family" "--language-arithmetic;--record-value-slice"
    "--family-source-draft;--language-arithmetic")
  execute_process(COMMAND "${ABCD_SEMANTIC}" missing.abc 300 ${options}
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
  string(JSON kind GET "${error}" error)
  if(status EQUAL 0 OR NOT output STREQUAL "" OR NOT kind EQUAL 0)
    message(FATAL_ERROR "invalid numeric selector reached file access: ${options}: ${error}")
  endif()
endforeach()
foreach(options IN ITEMS "--language-arithmetic" "--source;--language-arithmetic"
    "--language-arithmetic;--shared-source" "--value-source;--language-arithmetic;--language-arithmetic"
    "--value-source;--value-source" "--shared-source;--source")
  execute_process(COMMAND "${ABCD_STRUCTURE}" missing.abc unused-output ${options}
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(status EQUAL 0 OR NOT output STREQUAL "" OR NOT error MATCHES "^usage:")
    message(FATAL_ERROR "invalid structure selector reached file access: ${options}: ${error}")
  endif()
endforeach()
