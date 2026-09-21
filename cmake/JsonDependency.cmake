# Source identity and license: docs/reference/dependencies.md.
set(ABCD_JSON_ROOT "${CMAKE_SOURCE_DIR}/../arkruntime/third_party/json"
    CACHE PATH "Pinned nlohmann/json source directory")
set(ABCD_JSON_HEADER "${ABCD_JSON_ROOT}/single_include/nlohmann/json.hpp")
if(NOT EXISTS "${ABCD_JSON_HEADER}")
  message(FATAL_ERROR "Set ABCD_JSON_ROOT to the pinned JSON checkout")
endif()
file(SHA256 "${ABCD_JSON_HEADER}" ABCD_JSON_SHA256)
if(NOT ABCD_JSON_SHA256 STREQUAL
   "aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63")
  message(FATAL_ERROR "nlohmann/json header differs from the pinned dependency")
endif()
add_library(abcd_json INTERFACE)
target_include_directories(abcd_json SYSTEM INTERFACE "${ABCD_JSON_ROOT}/single_include")
