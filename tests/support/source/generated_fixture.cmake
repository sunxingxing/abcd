# Emit mode runs all the producer's assertions plus source serialization. A
# fixture shares that successful output with execution tests in this CTest run;
# it never skips a producer based on a previous run's output.
function(add_source_fixture target max_bytes timeout_ms)
  if(NOT ABCD_NODE_EXECUTABLE)
    add_test(NAME ${target} COMMAND ${target})
    return()
  endif()
  set(artifact "${CMAKE_CURRENT_BINARY_DIR}/source-fixtures/$<CONFIG>/${target}.json")
  add_test(NAME ${target} COMMAND ${ABCD_NODE_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tests/support/source/generated_fixture.mjs
    ${artifact} ${max_bytes} ${timeout_ms} $<TARGET_FILE:${target}> ${ARGN})
  set_tests_properties(${target} PROPERTIES FIXTURES_SETUP "source_${target}")
endfunction()

function(require_source_fixture consumer producer)
  set_tests_properties(${consumer} PROPERTIES FIXTURES_REQUIRED "source_${producer}")
  set_property(TEST ${consumer} APPEND PROPERTY ENVIRONMENT
    "ABCD_SOURCE_FIXTURE=${CMAKE_CURRENT_BINARY_DIR}/source-fixtures/$<CONFIG>/${producer}.json")
endfunction()
