cmake_minimum_required(VERSION 3.20)

project(abcd LANGUAGES CXX)
include(cmake/IsaDependency.cmake)

add_library(abcd_abc
  src/abc/byte_reader.cc
  src/abc/control_transfer.cc
  src/abc/header.cc
  src/abc/instruction.cc
  src/abc/metadata_cursor.cc
  src/abc/metadata_index.cc
  src/abc/method_body.cc
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
add_library(abcd_analysis src/analysis/normal_flow.cc)
target_link_libraries(abcd_analysis PUBLIC abcd_abc)
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
  add_library(abcd_inspection tools/corpus_inspection.cc)
  target_link_libraries(abcd_inspection PUBLIC abcd_corpus PRIVATE abcd_json abcd_analysis)
  add_executable(abc_index tools/abc_index.cc)
  target_link_libraries(abc_index PRIVATE abcd_inspection)
  add_executable(abc_decode tools/abc_decode.cc)
  target_link_libraries(abc_decode PRIVATE abcd_inspection)
  add_executable(abc_flow tools/abc_flow.cc)
  target_link_libraries(abc_flow PRIVATE abcd_inspection)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(control_transfer_test tests/unit/abc/control_transfer_test.cc)
  target_link_libraries(control_transfer_test PRIVATE abcd_abc)
  add_test(NAME control_transfer_test COMMAND control_transfer_test)

  add_executable(normal_flow_test tests/unit/analysis/normal_flow_test.cc)
  target_link_libraries(normal_flow_test PRIVATE abcd_analysis)
  add_test(NAME normal_flow_test COMMAND normal_flow_test)

  add_executable(byte_reader_test tests/unit/abc/byte_reader_test.cc)
  target_link_libraries(byte_reader_test PRIVATE abcd_abc)
  add_test(NAME byte_reader_test COMMAND byte_reader_test)

  add_executable(header_test tests/unit/abc/header_test.cc)
  target_link_libraries(header_test PRIVATE abcd_abc)
  add_test(NAME header_test COMMAND header_test)

  add_executable(metadata_index_test tests/unit/abc/metadata_index_test.cc)
  target_link_libraries(metadata_index_test PRIVATE abcd_abc)
  add_test(NAME metadata_index_test COMMAND metadata_index_test)

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
  endif()
endif()
