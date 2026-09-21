cmake_minimum_required(VERSION 3.20)

project(abcd LANGUAGES CXX)
include(cmake/IsaDependency.cmake)

add_library(abcd_abc
  src/abc/byte_reader.cc
  src/abc/control_transfer.cc
  src/abc/header.cc
  src/abc/instruction.cc
  src/abc/literal_array.cc
  src/abc/metadata_cursor.cc
  src/abc/metadata_index.cc
  src/abc/method_body.cc
  src/abc/method_references.cc
  src/abc/module_info.cc
  src/abc/pool_reference.cc
  src/abc/record_attributes.cc
  src/abc/register_access.cc
  src/abc/string_entity.cc
)
target_compile_features(abcd_abc PUBLIC cxx_std_20)
target_include_directories(abcd_abc PUBLIC include "${ABCD_ISA_GENERATED_DIR}")
add_dependencies(abcd_abc abcd_isa_generated)

find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
find_package(ZLIB REQUIRED)
include(cmake/JsonDependency.cmake)
add_library(abcd_file src/corpus/abc_file.cc)
target_compile_features(abcd_file PUBLIC cxx_std_20)
target_include_directories(abcd_file PUBLIC include)
target_link_libraries(abcd_file PUBLIC OpenSSL::Crypto ZLIB::ZLIB)
target_link_libraries(abcd_abc PUBLIC abcd_file)
add_library(abcd_analysis src/analysis/normal_flow.cc src/analysis/exception_dispatch.cc src/analysis/dominators.cc src/analysis/scalar.cc)
target_link_libraries(abcd_analysis PUBLIC abcd_abc)
add_library(abcd_source src/source/string_literal.cc)
target_compile_features(abcd_source PUBLIC cxx_std_20)
target_include_directories(abcd_source PUBLIC include)
add_library(abcd_corpus
  src/corpus/detect.cc
  src/corpus/manifest.cc
  src/corpus/manifest_json.cc
  src/corpus/manifest_entry.cc
  src/corpus/scan.cc
)
target_link_libraries(abcd_corpus PUBLIC abcd_abc
  PRIVATE OpenSSL::Crypto ZLIB::ZLIB abcd_json)

option(ABCD_BUILD_TOOLS "Build standalone ABC command-line tools" ON)
if(ABCD_BUILD_TOOLS)
  add_executable(abc_scan tools/abc_scan.cc)
  target_link_libraries(abc_scan PRIVATE abcd_corpus abcd_json)
  add_library(abcd_inspection tools/corpus_inspection.cc tools/method_inspection.cc tools/reference_inspection.cc tools/module_inspection.cc)
  target_link_libraries(abcd_inspection PUBLIC abcd_corpus PRIVATE abcd_json abcd_analysis)
  add_executable(abc_index tools/abc_index.cc)
  target_link_libraries(abc_index PRIVATE abcd_inspection)
  add_executable(abc_decode tools/abc_decode.cc)
  target_link_libraries(abc_decode PRIVATE abcd_inspection)
  add_executable(abc_flow tools/abc_flow.cc)
  target_link_libraries(abc_flow PRIVATE abcd_inspection)
  add_executable(abc_exceptions tools/abc_exceptions.cc)
  target_link_libraries(abc_exceptions PRIVATE abcd_inspection)
  add_executable(abc_references tools/abc_references.cc)
  target_link_libraries(abc_references PRIVATE abcd_inspection)
  add_executable(abc_modules tools/abc_modules.cc)
  target_link_libraries(abc_modules PRIVATE abcd_inspection)
  add_executable(abc_dominators tools/abc_dominators.cc)
  target_link_libraries(abc_dominators PRIVATE abcd_inspection)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(string_literal_test tests/unit/source/string_literal_test.cc)
  target_link_libraries(string_literal_test PRIVATE abcd_source)
  add_test(NAME string_literal_test COMMAND string_literal_test)
  find_program(ABCD_NODE_EXECUTABLE node)
  if(ABCD_NODE_EXECUTABLE)
    add_test(NAME string_literal_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_STRING_TEST=$<TARGET_FILE:string_literal_test>
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/string_literal.cmake)
  else()
    message(WARNING "Node unavailable: JS string roundtrip test not registered")
  endif()
  add_test(NAME isa_pin_test COMMAND ${CMAKE_COMMAND}
    -DABCD_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
    -DABCD_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -DABCD_ISA_ROOT=${ABCD_ISA_ROOT}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/isa_pin.cmake)

  add_executable(control_transfer_test tests/unit/abc/control_transfer_test.cc)
  target_link_libraries(control_transfer_test PRIVATE abcd_abc)
  add_test(NAME control_transfer_test COMMAND control_transfer_test)
  add_executable(register_access_test tests/unit/abc/register_access_test.cc)
  target_link_libraries(register_access_test PRIVATE abcd_abc)
  add_test(NAME register_access_test COMMAND register_access_test)

  add_executable(normal_flow_test tests/unit/analysis/normal_flow_test.cc)
  target_link_libraries(normal_flow_test PRIVATE abcd_analysis)
  add_test(NAME normal_flow_test COMMAND normal_flow_test)
  add_executable(dominators_test tests/unit/analysis/dominators_test.cc)
  target_link_libraries(dominators_test PRIVATE abcd_analysis)
  add_test(NAME dominators_test COMMAND dominators_test)
  add_executable(scalar_test tests/unit/analysis/scalar_test.cc)
  target_link_libraries(scalar_test PRIVATE abcd_analysis)
  add_test(NAME scalar_test COMMAND scalar_test)

  add_executable(exception_dispatch_test tests/unit/analysis/exception_dispatch_test.cc)
  target_link_libraries(exception_dispatch_test PRIVATE abcd_analysis)
  add_test(NAME exception_dispatch_test COMMAND exception_dispatch_test)

  add_executable(byte_reader_test tests/unit/abc/byte_reader_test.cc)
  target_link_libraries(byte_reader_test PRIVATE abcd_abc)
  add_test(NAME byte_reader_test COMMAND byte_reader_test)

  add_executable(header_test tests/unit/abc/header_test.cc)
  target_link_libraries(header_test PRIVATE abcd_abc)
  add_test(NAME header_test COMMAND header_test)

  add_executable(metadata_index_test tests/unit/abc/metadata_index_test.cc)
  target_link_libraries(metadata_index_test PRIVATE abcd_abc)
  add_test(NAME metadata_index_test COMMAND metadata_index_test)

  add_executable(pool_reference_test tests/unit/abc/pool_reference_test.cc)
  target_link_libraries(pool_reference_test PRIVATE abcd_abc)
  add_test(NAME pool_reference_test COMMAND pool_reference_test)

  add_executable(string_entity_test tests/unit/abc/string_entity_test.cc)
  target_link_libraries(string_entity_test PRIVATE abcd_abc)
  add_test(NAME string_entity_test COMMAND string_entity_test)

  add_executable(literal_array_test tests/unit/abc/literal_array_test.cc)
  target_link_libraries(literal_array_test PRIVATE abcd_abc)
  add_test(NAME literal_array_test COMMAND literal_array_test)

  add_executable(module_info_test tests/unit/abc/module_info_test.cc)
  target_link_libraries(module_info_test PRIVATE abcd_abc)
  add_test(NAME module_info_test COMMAND module_info_test)

  add_executable(record_attributes_test tests/unit/abc/record_attributes_test.cc)
  target_link_libraries(record_attributes_test PRIVATE abcd_abc)
  add_test(NAME record_attributes_test COMMAND record_attributes_test)

  add_executable(method_body_test tests/unit/abc/method_body_test.cc)
  target_link_libraries(method_body_test PRIVATE abcd_abc)
  add_test(NAME method_body_test COMMAND method_body_test)

  add_executable(instruction_test tests/unit/abc/instruction_test.cc)
  target_link_libraries(instruction_test PRIVATE abcd_abc)
  add_dependencies(instruction_test abcd_isa_generated)
  add_test(NAME instruction_test COMMAND instruction_test)

  add_executable(abc_file_test tests/unit/corpus/abc_file_test.cc)
  target_link_libraries(abc_file_test PRIVATE abcd_corpus)
  add_test(NAME abc_file_test COMMAND abc_file_test)

  add_executable(detect_test tests/unit/corpus/detect_test.cc)
  target_link_libraries(detect_test PRIVATE abcd_corpus ZLIB::ZLIB)
  add_test(NAME detect_test COMMAND detect_test)

  add_executable(manifest_test tests/unit/corpus/manifest_test.cc)
  target_link_libraries(manifest_test PRIVATE abcd_corpus)
  add_test(NAME manifest_test COMMAND manifest_test)

  add_executable(scan_test tests/unit/corpus/scan_test.cc)
  target_link_libraries(scan_test PRIVATE abcd_corpus ZLIB::ZLIB)
  add_test(NAME scan_test COMMAND scan_test)

  if(TARGET abc_scan)
    add_test(NAME modules_cli_test COMMAND ${CMAKE_COMMAND}
      -DABCD_SCAN=$<TARGET_FILE:abc_scan>
      -DABCD_MODULES=$<TARGET_FILE:abc_modules>
      -DABCD_FIXTURE=$<TARGET_FILE:index_fixture>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/modules_cli.cmake)
    add_test(NAME dominators_cli_test COMMAND ${CMAKE_COMMAND}
      -DABCD_SCAN=$<TARGET_FILE:abc_scan>
      -DABCD_DOMINATORS=$<TARGET_FILE:abc_dominators>
      -DABCD_FIXTURE=$<TARGET_FILE:index_fixture>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/dominators_cli.cmake)
    add_test(NAME references_cli_test COMMAND ${CMAKE_COMMAND}
      -DABCD_SCAN=$<TARGET_FILE:abc_scan>
      -DABCD_REFERENCES=$<TARGET_FILE:abc_references>
      -DABCD_FIXTURE=$<TARGET_FILE:index_fixture>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/references_cli.cmake)
    add_test(NAME scan_cli_test COMMAND ${CMAKE_COMMAND}
      -DABCD_SCAN=$<TARGET_FILE:abc_scan>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/scan_cli.cmake)
    add_executable(index_fixture tests/integration/index_fixture.cc)
    target_link_libraries(index_fixture PRIVATE abcd_abc ZLIB::ZLIB)
    add_test(NAME index_cli_test COMMAND ${CMAKE_COMMAND}
      -DABCD_SCAN=$<TARGET_FILE:abc_scan>
      -DABCD_INDEX=$<TARGET_FILE:abc_index>
      -DABCD_FIXTURE=$<TARGET_FILE:index_fixture>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/index_cli.cmake)
    add_test(NAME decode_cli_test COMMAND ${CMAKE_COMMAND}
      -DABCD_SCAN=$<TARGET_FILE:abc_scan>
      -DABCD_DECODE=$<TARGET_FILE:abc_decode>
      -DABCD_FIXTURE=$<TARGET_FILE:index_fixture>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/decode_cli.cmake)
    add_test(NAME flow_cli_test COMMAND ${CMAKE_COMMAND}
      -DABCD_SCAN=$<TARGET_FILE:abc_scan>
      -DABCD_FLOW=$<TARGET_FILE:abc_flow>
      -DABCD_FIXTURE=$<TARGET_FILE:index_fixture>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/flow_cli.cmake)
    add_test(NAME exceptions_cli_test COMMAND ${CMAKE_COMMAND}
      -DABCD_SCAN=$<TARGET_FILE:abc_scan>
      -DABCD_EXCEPTIONS=$<TARGET_FILE:abc_exceptions>
      -DABCD_FIXTURE=$<TARGET_FILE:index_fixture>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/exceptions_cli.cmake)
  endif()
endif()
