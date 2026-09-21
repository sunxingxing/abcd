set(ABCD_FLOW "${ABCD_EXCEPTIONS}")
include("${CMAKE_CURRENT_LIST_DIR}/flow_cli.cmake")
execute_process(COMMAND "${ABCD_FIXTURE}" "${root}/exceptions" --exception-method
  RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "exception fixture generation failed")
endif()
execute_process(COMMAND "${ABCD_SCAN}" "${root}/exceptions.json" fixture "${root}/exceptions"
  RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "exception fixture classification failed")
endif()
execute_process(COMMAND "${ABCD_EXCEPTIONS}" "${root}/exceptions.json" fixture "${root}/exceptions"
  RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_VARIABLE diagnostic)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "exception inspection failed: ${diagnostic}")
endif()
foreach(key IN ITEMS dispatch_methods failed_dispatch_methods dispatch_spans
    protected_blocks protected_instructions flow_methods blocks try_regions catch_handlers)
  string(JSON ${key} GET "${report}" ${key})
endforeach()
string(JSON stage GET "${report}" stage)
string(JSON outcome GET "${report}" results 0 outcome)
if(NOT dispatch_methods EQUAL 1 OR NOT failed_dispatch_methods EQUAL 0
    OR NOT dispatch_spans EQUAL 1 OR NOT protected_blocks EQUAL 1
    OR NOT protected_instructions EQUAL 2 OR NOT flow_methods EQUAL 1
    OR NOT blocks EQUAL 3 OR NOT try_regions EQUAL 1 OR NOT catch_handlers EQUAL 2
    OR NOT stage STREQUAL "exception_dispatch" OR NOT outcome STREQUAL "dispatch_built")
  message(FATAL_ERROR "incorrect exception dispatch counts: ${report}")
endif()
file(REMOVE_RECURSE "${root}")
