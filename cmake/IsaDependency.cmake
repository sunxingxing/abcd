# Pinned OpenHarmony-v7.0-Release runtime_core ISA generator inputs.
set(ABCD_ISA_ROOT
    "${CMAKE_SOURCE_DIR}/../arkruntime/arkcompiler/runtime_core/isa"
    CACHE PATH "Pinned runtime_core ISA directory")
set(ABCD_ISA_YAML "${ABCD_ISA_ROOT}/isa.yaml")
set(ABCD_ISA_API "${ABCD_ISA_ROOT}/isapi.rb")
set(ABCD_ISA_GEN "${ABCD_ISA_ROOT}/gen.rb")

foreach(input IN ITEMS
    "${ABCD_ISA_YAML}|690d960793e07d18c94d81a644e916592bb29962c48c7e2f5f48376989f530f9"
    "${ABCD_ISA_API}|d45122d4ab8697812f21eac101ac10f04b314560ec0723bfb44f676206f729ce"
    "${ABCD_ISA_GEN}|43c2ab419deb1090a8a897a1b50ec48d42f0a7a5bcd6a5a6e5b210be1f7902fb")
  string(REPLACE "|" ";" pair "${input}")
  list(GET pair 0 path)
  list(GET pair 1 expected)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Pinned ISA input missing: ${path}")
  endif()
  file(SHA256 "${path}" actual)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "Pinned ISA input changed: ${path}")
  endif()
endforeach()

find_package(Ruby 3.2.3 EXACT REQUIRED)
set(ABCD_ISA_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(ABCD_ISA_OPCODE_HEADER
    "${ABCD_ISA_GENERATED_DIR}/abcd/abc/generated_isa.h")
set(ABCD_ISA_TABLE
    "${ABCD_ISA_GENERATED_DIR}/abcd/abc/generated_isa_table.inc")
set(ABCD_ISA_CASES
    "${ABCD_ISA_GENERATED_DIR}/abcd/abc/generated_isa_cases.inc")
add_custom_command(
  OUTPUT "${ABCD_ISA_OPCODE_HEADER}" "${ABCD_ISA_TABLE}"
         "${ABCD_ISA_CASES}"
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${ABCD_ISA_GENERATED_DIR}/abcd/abc"
  COMMAND ${Ruby_EXECUTABLE} "${ABCD_ISA_GEN}"
          -d "${ABCD_ISA_YAML}" -r "${ABCD_ISA_API}"
          -t "${CMAKE_CURRENT_SOURCE_DIR}/tools/templates/isa_opcode.h.erb"
          -o "${ABCD_ISA_OPCODE_HEADER}"
  COMMAND ${Ruby_EXECUTABLE} "${ABCD_ISA_GEN}"
          -d "${ABCD_ISA_YAML}" -r "${ABCD_ISA_API}"
          -t "${CMAKE_CURRENT_SOURCE_DIR}/tools/templates/isa_table.inc.erb"
          -o "${ABCD_ISA_TABLE}"
  COMMAND ${Ruby_EXECUTABLE} "${ABCD_ISA_GEN}"
          -d "${ABCD_ISA_YAML}" -r "${ABCD_ISA_API}"
          -t "${CMAKE_CURRENT_SOURCE_DIR}/tools/templates/isa_cases.inc.erb"
          -o "${ABCD_ISA_CASES}"
  DEPENDS "${ABCD_ISA_GEN}" "${ABCD_ISA_API}" "${ABCD_ISA_YAML}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/templates/isa_opcode.h.erb"
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/templates/isa_table.inc.erb"
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/templates/isa_cases.inc.erb"
  VERBATIM)
add_custom_target(abcd_isa_generated
  DEPENDS "${ABCD_ISA_OPCODE_HEADER}" "${ABCD_ISA_TABLE}"
          "${ABCD_ISA_CASES}")
