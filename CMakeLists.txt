cmake_minimum_required(VERSION 3.20)

project(abcd LANGUAGES CXX)
include(cmake/IsaDependency.cmake)

add_library(abcd_abc
  src/abc/byte_reader.cc
  src/abc/call_type.cc
  src/abc/control_transfer.cc
  src/abc/debug_info.cc
  src/abc/debug_scopes.cc
  src/abc/frame_layout.cc
  src/abc/header.cc
  src/abc/instruction.cc
  src/abc/literal_array.cc
  src/abc/metadata_cursor.cc
  src/abc/metadata_index.cc
  src/abc/method_body.cc
  src/abc/method_attributes.cc
  src/abc/method_lookup.cc
  src/abc/method_target.cc
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
target_sources(abcd_analysis PRIVATE src/analysis/branch_condition.cc)
target_sources(abcd_analysis PRIVATE src/analysis/function_creation.cc)
target_sources(abcd_analysis PRIVATE src/analysis/creation_binding.cc)
target_sources(abcd_analysis PRIVATE src/analysis/capture_summary.cc)
target_sources(abcd_analysis PRIVATE src/analysis/method_captures.cc)
target_sources(abcd_analysis PRIVATE src/analysis/capture_propagation.cc)
target_sources(abcd_analysis PRIVATE src/analysis/lexical.cc)
target_sources(abcd_analysis PRIVATE src/analysis/environment_index.cc)
target_sources(abcd_analysis PRIVATE src/analysis/environment_flow.cc)
target_sources(abcd_analysis PRIVATE src/analysis/hole_guard.cc)
target_sources(abcd_analysis PRIVATE src/analysis/indexed_read.cc)
target_sources(abcd_analysis PRIVATE src/analysis/named_read.cc)
target_sources(abcd_analysis PRIVATE src/analysis/named_write.cc)
target_sources(abcd_analysis PRIVATE src/analysis/ordinary_call.cc)
target_sources(abcd_analysis PRIVATE src/analysis/named_invocation.cc)
target_sources(abcd_analysis PRIVATE src/analysis/phi_placement.cc src/analysis/iterated_frontier.cc)
target_sources(abcd_analysis PRIVATE src/analysis/binding_liveness.cc)
target_sources(abcd_analysis PRIVATE src/analysis/register_effect.cc)
target_sources(abcd_analysis PRIVATE src/analysis/register_uses.cc)
target_sources(abcd_analysis PRIVATE src/analysis/register_ssa.cc src/analysis/register_rename.cc)
target_sources(abcd_analysis PRIVATE src/analysis/register_value_flow.cc src/analysis/register_value_rule.cc src/analysis/lexical_value_index.cc)
target_sources(abcd_analysis PRIVATE src/analysis/guarded_reads.cc)
target_sources(abcd_analysis PRIVATE src/analysis/environment_escape.cc src/analysis/environment_escape_aliases.cc)
target_sources(abcd_analysis PRIVATE src/analysis/linear_lexicals.cc)
target_sources(abcd_analysis PRIVATE src/analysis/block_order.cc)
target_sources(abcd_analysis PRIVATE src/analysis/accumulator_flow.cc src/analysis/scalar_access.cc)
target_link_libraries(abcd_analysis PUBLIC abcd_abc)
add_library(abcd_source src/source/string_literal.cc src/source/scalar_expression.cc)
target_sources(abcd_source PRIVATE src/source/linear_body.cc)
target_sources(abcd_source PRIVATE src/source/acyclic_body.cc)
target_sources(abcd_source PRIVATE src/source/local_lexical_body.cc)
target_sources(abcd_source PRIVATE src/source/base_function.cc)
target_sources(abcd_source PRIVATE src/source/created_function.cc)
target_sources(abcd_source PRIVATE src/source/leaf_creation.cc)
target_sources(abcd_source PRIVATE src/source/leaf_sources.cc)
target_link_libraries(abcd_source PUBLIC abcd_analysis)
target_compile_features(abcd_source PUBLIC cxx_std_20)
target_include_directories(abcd_source PUBLIC include)
add_library(abcd_structure src/structure/record_path.cc src/structure/record_writer.cc src/structure/module_writer.cc)
target_sources(abcd_structure PRIVATE src/structure/debug_writer.cc)
target_sources(abcd_structure PRIVATE src/structure/scope_writer.cc)
target_link_libraries(abcd_structure PUBLIC abcd_source)
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
  add_executable(abc_structure tools/abc_structure.cc)
  target_link_libraries(abc_structure PRIVATE abcd_structure abcd_corpus abcd_json)
  add_executable(abc_scan tools/abc_scan.cc)
  target_link_libraries(abc_scan PRIVATE abcd_corpus abcd_json)
  add_library(abcd_inspection tools/corpus_inspection.cc tools/method_inspection.cc tools/reference_inspection.cc tools/module_inspection.cc)
  target_sources(abcd_inspection PRIVATE tools/source_inspection.cc)
  target_sources(abcd_inspection PRIVATE tools/environment_inspection.cc)
  target_sources(abcd_inspection PRIVATE tools/creation_inspection.cc)
  target_link_libraries(abcd_inspection PUBLIC abcd_corpus PRIVATE abcd_json abcd_source)
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
  add_executable(abc_frames tools/abc_frames.cc)
  target_link_libraries(abc_frames PRIVATE abcd_inspection)
  add_executable(abc_source_probe tools/abc_source_probe.cc)
  target_link_libraries(abc_source_probe PRIVATE abcd_inspection)
  add_executable(abc_environments tools/abc_environments.cc)
  target_link_libraries(abc_environments PRIVATE abcd_inspection)
  add_executable(abc_creations tools/abc_creations.cc)
  target_link_libraries(abc_creations PRIVATE abcd_inspection)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(record_path_test tests/unit/structure/record_path_test.cc)
  target_link_libraries(record_path_test PRIVATE abcd_structure)
  add_test(NAME record_path_test COMMAND record_path_test)
  add_executable(module_writer_test tests/unit/structure/module_writer_test.cc)
  target_include_directories(module_writer_test PRIVATE src/structure)
  target_link_libraries(module_writer_test PRIVATE abcd_structure)
  add_test(NAME module_writer_test COMMAND module_writer_test)
  add_executable(debug_info_test tests/unit/abc/debug_info_test.cc)
  target_link_libraries(debug_info_test PRIVATE abcd_abc)
  add_test(NAME debug_info_test COMMAND debug_info_test)
  add_executable(debug_scopes_test tests/unit/abc/debug_scopes_test.cc)
  target_link_libraries(debug_scopes_test PRIVATE abcd_abc)
  add_test(NAME debug_scopes_test COMMAND debug_scopes_test)
  add_executable(capture_summary_test tests/unit/analysis/capture_summary_test.cc)
  target_link_libraries(capture_summary_test PRIVATE abcd_analysis)
  add_test(NAME capture_summary_test COMMAND capture_summary_test)
  add_executable(capture_rebase_test tests/unit/analysis/capture_rebase_test.cc)
  target_link_libraries(capture_rebase_test PRIVATE abcd_analysis)
  add_test(NAME capture_rebase_test COMMAND capture_rebase_test)
  add_executable(creation_binding_test tests/unit/analysis/creation_binding_test.cc)
  target_link_libraries(creation_binding_test PRIVATE abcd_analysis)
  add_test(NAME creation_binding_test COMMAND creation_binding_test)
  add_executable(capture_demand_test tests/unit/analysis/capture_demand_test.cc)
  target_link_libraries(capture_demand_test PRIVATE abcd_analysis)
  add_test(NAME capture_demand_test COMMAND capture_demand_test)
  add_executable(capture_propagation_test tests/unit/analysis/capture_propagation_test.cc)
  target_link_libraries(capture_propagation_test PRIVATE abcd_analysis)
  add_test(NAME capture_propagation_test COMMAND capture_propagation_test)
  add_executable(method_lookup_test tests/unit/abc/method_lookup_test.cc)
  target_link_libraries(method_lookup_test PRIVATE abcd_abc)
  add_test(NAME method_lookup_test COMMAND method_lookup_test)
  add_executable(environment_flow_test tests/unit/analysis/environment_flow_test.cc)
  target_link_libraries(environment_flow_test PRIVATE abcd_analysis)
  add_test(NAME environment_flow_test COMMAND environment_flow_test)
  add_executable(environment_index_test tests/unit/analysis/environment_index_test.cc)
  target_link_libraries(environment_index_test PRIVATE abcd_analysis)
  add_test(NAME environment_index_test COMMAND environment_index_test)
  add_executable(acyclic_body_test tests/unit/source/acyclic_body_test.cc)
  target_link_libraries(acyclic_body_test PRIVATE abcd_source)
  add_test(NAME acyclic_body_test COMMAND acyclic_body_test)
  add_executable(block_loop_body_test tests/unit/source/block_loop_body_test.cc tests/support/analysis/block_loop_order.cc)
  target_include_directories(block_loop_body_test PRIVATE tests/support)
  target_link_libraries(block_loop_body_test PRIVATE abcd_source)
  add_test(NAME block_loop_body_test COMMAND block_loop_body_test)
  add_executable(accumulator_flow_test tests/unit/analysis/accumulator_flow_test.cc)
  target_link_libraries(accumulator_flow_test PRIVATE abcd_analysis)
  add_test(NAME accumulator_flow_test COMMAND accumulator_flow_test)
  add_executable(block_order_test tests/unit/analysis/block_order_test.cc)
  target_link_libraries(block_order_test PRIVATE abcd_analysis)
  add_test(NAME block_order_test COMMAND block_order_test)
  add_executable(named_read_test tests/unit/analysis/named_read_test.cc)
  target_link_libraries(named_read_test PRIVATE abcd_analysis)
  add_test(NAME named_read_test COMMAND named_read_test)
  add_executable(named_write_test tests/unit/analysis/named_write_test.cc)
  target_link_libraries(named_write_test PRIVATE abcd_analysis)
  add_test(NAME named_write_test COMMAND named_write_test)
  add_executable(ordinary_call_test tests/unit/analysis/ordinary_call_test.cc)
  target_link_libraries(ordinary_call_test PRIVATE abcd_source)
  add_test(NAME ordinary_call_test COMMAND ordinary_call_test)
  add_executable(indexed_read_test tests/unit/analysis/indexed_read_test.cc)
  target_link_libraries(indexed_read_test PRIVATE abcd_analysis)
  add_test(NAME indexed_read_test COMMAND indexed_read_test)
  add_executable(hole_guard_test tests/unit/analysis/hole_guard_test.cc)
  target_link_libraries(hole_guard_test PRIVATE abcd_analysis)
  add_test(NAME hole_guard_test COMMAND hole_guard_test)
  add_executable(lexical_test tests/unit/analysis/lexical_test.cc)
  target_link_libraries(lexical_test PRIVATE abcd_analysis)
  add_test(NAME lexical_test COMMAND lexical_test)
  add_executable(function_creation_test tests/unit/analysis/function_creation_test.cc)
  target_link_libraries(function_creation_test PRIVATE abcd_analysis)
  add_test(NAME function_creation_test COMMAND function_creation_test)
  add_executable(method_attributes_test tests/unit/abc/method_attributes_test.cc)
  target_link_libraries(method_attributes_test PRIVATE abcd_abc)
  add_test(NAME method_attributes_test COMMAND method_attributes_test)
  add_executable(linear_constants_test tests/unit/source/linear_constants_test.cc)
  target_link_libraries(linear_constants_test PRIVATE abcd_source)
  add_test(NAME linear_constants_test COMMAND linear_constants_test)
  add_executable(named_call_test tests/unit/source/named_call_test.cc)
  target_include_directories(named_call_test PRIVATE tests/support)
  target_link_libraries(named_call_test PRIVATE abcd_source)
  add_test(NAME named_call_test COMMAND named_call_test)
  add_executable(linear_body_test tests/unit/source/linear_body_test.cc)
  target_link_libraries(linear_body_test PRIVATE abcd_source)
  add_test(NAME linear_body_test COMMAND linear_body_test)
  add_executable(base_function_test tests/unit/source/base_function_test.cc)
  target_include_directories(base_function_test PRIVATE tests/support)
  target_link_libraries(base_function_test PRIVATE abcd_source)
  add_test(NAME base_function_test COMMAND base_function_test)
  add_executable(created_function_test tests/unit/source/created_function_test.cc tests/support/source/leaf_function_checks.cc)
  target_include_directories(created_function_test PRIVATE tests/support src/source)
  target_link_libraries(created_function_test PRIVATE abcd_source)
  add_test(NAME created_function_test COMMAND created_function_test)
  add_executable(local_lexical_body_test tests/unit/source/local_lexical_body_test.cc tests/support/analysis/local_lexical_scaling.cc)
  target_include_directories(local_lexical_body_test PRIVATE tests/support)
  target_link_libraries(local_lexical_body_test PRIVATE abcd_source)
  add_test(NAME local_lexical_body_test COMMAND local_lexical_body_test)
  add_executable(branch_condition_test tests/unit/analysis/branch_condition_test.cc)
  target_link_libraries(branch_condition_test PRIVATE abcd_analysis)
  add_test(NAME branch_condition_test COMMAND branch_condition_test)
  add_executable(call_type_test tests/unit/abc/call_type_test.cc)
  target_link_libraries(call_type_test PRIVATE abcd_abc)
  add_test(NAME call_type_test COMMAND call_type_test)
  add_executable(frame_layout_test tests/unit/abc/frame_layout_test.cc)
  target_link_libraries(frame_layout_test PRIVATE abcd_abc)
  add_test(NAME frame_layout_test COMMAND frame_layout_test)
  add_executable(string_literal_test tests/unit/source/string_literal_test.cc)
  target_link_libraries(string_literal_test PRIVATE abcd_source)
  add_test(NAME string_literal_test COMMAND string_literal_test)
  add_executable(scalar_expression_test tests/unit/source/scalar_expression_test.cc)
  target_link_libraries(scalar_expression_test PRIVATE abcd_source)
  add_test(NAME scalar_expression_test COMMAND scalar_expression_test)
  find_program(ABCD_NODE_EXECUTABLE node)
  if(ABCD_NODE_EXECUTABLE)
    add_test(NAME created_function_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:created_function_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/created_function.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=created-function.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    add_test(NAME base_function_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:base_function_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/base_function.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=base-function.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    add_test(NAME local_lexical_body_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:local_lexical_body_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/local_lexical_body.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=local-lexical-body.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    add_test(NAME block_loop_body_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:block_loop_body_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/block_loop_body.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=block-loop-body.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    add_test(NAME ordinary_call_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:ordinary_call_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/ordinary_call.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=ordinary-call.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    add_test(NAME acyclic_body_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:acyclic_body_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/acyclic_body.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=acyclic-body.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    add_test(NAME named_call_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:named_call_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/named_call.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=named-call.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    add_test(NAME linear_constants_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:linear_constants_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    add_executable(scalar_source_fixture tests/integration/scalar_source_fixture.cc)
    target_link_libraries(scalar_source_fixture PRIVATE abcd_source)
    add_test(NAME scalar_source_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_SCALAR_FIXTURE=$<TARGET_FILE:scalar_source_fixture>
      -DABCD_SCALAR_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/scalar_source.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/scalar_source.cmake)
    add_test(NAME string_literal_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_STRING_TEST=$<TARGET_FILE:string_literal_test>
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/string_literal.cmake)
  else()
    message(WARNING "Node unavailable: JS source oracle tests not registered")
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
  add_executable(binding_liveness_test tests/unit/analysis/binding_liveness_test.cc)
  target_link_libraries(binding_liveness_test PRIVATE abcd_analysis)
  add_test(NAME binding_liveness_test COMMAND binding_liveness_test)
  add_executable(register_effect_test tests/unit/analysis/register_effect_test.cc)
  target_link_libraries(register_effect_test PRIVATE abcd_analysis)
  add_test(NAME register_effect_test COMMAND register_effect_test)
  add_executable(register_uses_test tests/unit/analysis/register_uses_test.cc)
  target_link_libraries(register_uses_test PRIVATE abcd_analysis)
  add_test(NAME register_uses_test COMMAND register_uses_test)
  add_executable(register_ssa_test tests/unit/analysis/register_ssa_test.cc tests/support/analysis/ssa_paths.cc)
  target_include_directories(register_ssa_test PRIVATE tests/support)
  target_link_libraries(register_ssa_test PRIVATE abcd_analysis)
  add_test(NAME register_ssa_test COMMAND register_ssa_test)
  add_executable(register_value_flow_test tests/unit/analysis/register_value_flow_test.cc tests/support/analysis/value_flow_oracle.cc tests/support/analysis/value_flow_scaling.cc)
  target_include_directories(register_value_flow_test PRIVATE tests/support)
  target_link_libraries(register_value_flow_test PRIVATE abcd_analysis)
  add_test(NAME register_value_flow_test COMMAND register_value_flow_test)
  add_executable(guarded_reads_test tests/unit/analysis/guarded_reads_test.cc tests/support/analysis/guard_oracle.cc tests/support/analysis/guard_scaling.cc)
  target_include_directories(guarded_reads_test PRIVATE tests/support)
  target_link_libraries(guarded_reads_test PRIVATE abcd_analysis)
  add_test(NAME guarded_reads_test COMMAND guarded_reads_test)
  add_executable(environment_escape_test tests/unit/analysis/environment_escape_test.cc tests/support/analysis/escape_oracle.cc tests/support/analysis/escape_scaling.cc)
  target_include_directories(environment_escape_test PRIVATE tests/support)
  target_link_libraries(environment_escape_test PRIVATE abcd_analysis)
  add_test(NAME environment_escape_test COMMAND environment_escape_test)
  add_executable(lexical_value_flow_test tests/unit/analysis/lexical_value_flow_test.cc tests/support/analysis/lexical_value_oracle.cc tests/support/analysis/lexical_value_scaling.cc)
  target_include_directories(lexical_value_flow_test PRIVATE tests/support)
  target_link_libraries(lexical_value_flow_test PRIVATE abcd_analysis)
  add_test(NAME lexical_value_flow_test COMMAND lexical_value_flow_test)
  add_executable(phi_placement_test tests/unit/analysis/phi_placement_test.cc)
  target_link_libraries(phi_placement_test PRIVATE abcd_analysis)
  add_test(NAME phi_placement_test COMMAND phi_placement_test)
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
    add_test(NAME structure_cli_test COMMAND ${CMAKE_COMMAND}
      -DABCD_STRUCTURE=$<TARGET_FILE:abc_structure>
      -DABCD_FIXTURE=$<TARGET_FILE:index_fixture>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/structure_cli.cmake)
    add_test(NAME creations_cli_test COMMAND ${CMAKE_COMMAND}
      -DABCD_SCAN=$<TARGET_FILE:abc_scan>
      -DABCD_CREATIONS=$<TARGET_FILE:abc_creations>
      -DABCD_FIXTURE=$<TARGET_FILE:index_fixture>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/creations_cli.cmake)
    add_test(NAME environments_cli_test COMMAND ${CMAKE_COMMAND}
      -DABCD_SCAN=$<TARGET_FILE:abc_scan>
      -DABCD_ENVIRONMENTS=$<TARGET_FILE:abc_environments>
      -DABCD_FIXTURE=$<TARGET_FILE:index_fixture>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/environments_cli.cmake)
    add_test(NAME source_probe_cli_test COMMAND ${CMAKE_COMMAND}
      -DABCD_SCAN=$<TARGET_FILE:abc_scan>
      -DABCD_PROBE=$<TARGET_FILE:abc_source_probe>
      -DABCD_FIXTURE=$<TARGET_FILE:index_fixture>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/source_probe_cli.cmake)
    add_test(NAME frames_cli_test COMMAND ${CMAKE_COMMAND}
      -DABCD_SCAN=$<TARGET_FILE:abc_scan>
      -DABCD_FRAMES=$<TARGET_FILE:abc_frames>
      -DABCD_FIXTURE=$<TARGET_FILE:index_fixture>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/frames_cli.cmake)
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
