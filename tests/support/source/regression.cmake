# The exact accepted gate is versioned, including its build dependencies.
# Missing optional corpus/Node registrations must not silently shrink the gate.
if(ABCD_SOURCE_REGRESSION)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/tests/source-regression.json")
  file(READ "${PROJECT_SOURCE_DIR}/tests/source-regression.json" selection)
  string(JSON count LENGTH "${selection}" names)
  math(EXPR last "${count} - 1")
  foreach(index RANGE ${last})
    string(JSON name GET "${selection}" names ${index})
    if(NOT TEST "${name}")
      message(FATAL_ERROR "Source regression test missing: ${name}; configure Node and the paired corpus")
    endif()
    set_property(TEST "${name}" APPEND PROPERTY LABELS source-regression)
  endforeach()
  string(JSON count LENGTH "${selection}" targets)
  math(EXPR last "${count} - 1")
  set(targets)
  foreach(index RANGE ${last})
    string(JSON name GET "${selection}" targets ${index})
    if(NOT TARGET "${name}")
      message(FATAL_ERROR "Source regression target missing: ${name}")
    endif()
    list(APPEND targets "${name}")
  endforeach()
  add_library(source_regression_assertions OBJECT
    "${PROJECT_SOURCE_DIR}/tests/support/source/regression_assertions.cc")
  add_custom_target(source_regression DEPENDS ${targets} source_regression_assertions)
endif()

# Whole-application runtime differentials (minutes each, memoized by content
# in tests/integration/result_cache.mjs). Exclude with `-LE corpus-graph` for
# quick reviews; the commit gate runs the full suite, where unchanged inputs
# reuse the passing verdict. ABCD_TEST_CACHE=off forces recomputation.
foreach(name IN ITEMS navigation_graph_source_test minigame_navigation_graph_source_test
    window_navigation_graph_source_test community_graph_source_test
    community_ability_graph_source_test shopping_navigation_graph_source_test_0
    shopping_navigation_graph_source_test_1 shopping_navigation_graph_source_test_2
    shopping_navigation_graph_source_test_3)
  if(TEST "${name}")
    set_property(TEST "${name}" APPEND PROPERTY LABELS corpus-graph)
  endif()
endforeach()
