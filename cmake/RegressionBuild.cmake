# This is a separate execution configuration, not a reduced test selection.
# Keep Debug symbols and assertions; never use Release/NDEBUG to speed a gate.
if(ABCD_SOURCE_REGRESSION)
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR "Source regression requires a single-configuration Debug build")
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang|GNU)$")
    add_compile_options(-O2 -g -UNDEBUG)
  else()
    message(FATAL_ERROR "Source regression optimization is not configured for this compiler")
  endif()
endif()
