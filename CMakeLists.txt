cmake_minimum_required(VERSION 3.20)

project(abcd LANGUAGES CXX)
include(cmake/IsaDependency.cmake)

add_library(abcd_abc
  src/abc/byte_reader.cc
  src/abc/call_type.cc
  src/abc/class_buffer.cc
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
target_sources(abcd_analysis PRIVATE src/analysis/class_method_sites.cc)
target_sources(abcd_analysis PRIVATE src/analysis/branch_condition.cc)
target_sources(abcd_analysis PRIVATE src/analysis/semantic_ir.cc)
target_sources(abcd_analysis PRIVATE src/analysis/value_operands.cc)
target_sources(abcd_analysis PRIVATE src/analysis/call_operands.cc)
target_sources(abcd_analysis PRIVATE src/analysis/operand_view.cc)
target_sources(abcd_analysis PRIVATE src/analysis/value_verify.cc)
target_sources(abcd_analysis PRIVATE src/analysis/value_flow.cc)
target_sources(abcd_analysis PRIVATE src/analysis/value_edits.cc)
target_sources(abcd_analysis PRIVATE src/analysis/value_simplify.cc)
target_sources(abcd_analysis PRIVATE src/analysis/value_phi.cc)
target_sources(abcd_analysis PRIVATE src/analysis/value_rewrite.cc)
target_sources(abcd_analysis PRIVATE src/analysis/value_import.cc)
target_sources(abcd_analysis PRIVATE src/analysis/value_correspondence.cc)
target_sources(abcd_analysis PRIVATE src/analysis/semantic_value_rule.cc)
target_sources(abcd_analysis PRIVATE src/analysis/semantic_region.cc
  src/analysis/loop_intervals.cc)
target_sources(abcd_analysis PRIVATE src/analysis/exception_edge.cc)
target_sources(abcd_analysis PRIVATE src/analysis/exception_flow.cc)
target_sources(abcd_analysis PRIVATE src/analysis/exception_ssa.cc)
target_sources(abcd_analysis PRIVATE src/analysis/exception_value_flow.cc)
target_sources(abcd_analysis PRIVATE src/analysis/exception_environment.cc)
target_sources(abcd_analysis PRIVATE src/analysis/exception_inputs.cc)
target_sources(abcd_analysis PRIVATE src/analysis/exception_regions.cc)
target_sources(abcd_analysis PRIVATE src/analysis/exception_region_tree.cc)
target_sources(abcd_analysis PRIVATE src/analysis/instruction_ssa.cc)
target_sources(abcd_analysis PRIVATE src/analysis/exception_locals.cc)
target_sources(abcd_analysis PRIVATE src/analysis/exception_modules.cc)
target_sources(abcd_analysis PRIVATE src/analysis/exception_calls.cc)
target_sources(abcd_analysis PRIVATE src/analysis/exception_captures.cc)
target_sources(abcd_analysis PRIVATE src/analysis/semantic_validation.cc
  src/analysis/semantic_locals.cc)
target_sources(abcd_analysis PRIVATE src/analysis/constant_object.cc)
target_sources(abcd_analysis PRIVATE src/analysis/iterator.cc)
target_sources(abcd_analysis PRIVATE src/analysis/coroutine.cc)
target_sources(abcd_analysis PRIVATE src/analysis/await_region.cc src/analysis/await_catch.cc)
target_sources(abcd_analysis PRIVATE src/analysis/iter_result.cc)
target_sources(abcd_analysis PRIVATE src/analysis/rest_arguments.cc)
target_sources(abcd_analysis PRIVATE src/analysis/first_for_in.cc)
target_sources(abcd_analysis PRIVATE src/analysis/private_property.cc)
target_sources(abcd_analysis PRIVATE src/analysis/guarded_global_call.cc)
target_sources(abcd_analysis PRIVATE src/analysis/function_creation.cc)
target_sources(abcd_analysis PRIVATE src/analysis/define_instruction.cc)
target_sources(abcd_analysis PRIVATE src/analysis/class_creation.cc)
target_sources(abcd_analysis PRIVATE src/analysis/class_heritage.cc)
target_sources(abcd_analysis PRIVATE src/analysis/derived_constructor.cc)
target_sources(abcd_analysis PRIVATE src/analysis/range_derived_constructor.cc)
target_sources(abcd_analysis PRIVATE src/analysis/local_ssa.cc)
target_sources(abcd_analysis PRIVATE src/analysis/spread_field_constructor.cc)
target_sources(abcd_analysis PRIVATE src/analysis/method_roles.cc)
target_sources(abcd_analysis PRIVATE src/analysis/creation_binding.cc)
target_sources(abcd_analysis PRIVATE src/analysis/capture_summary.cc)
target_sources(abcd_analysis PRIVATE src/analysis/method_captures.cc)
target_sources(abcd_analysis PRIVATE src/analysis/capture_propagation.cc)
target_sources(abcd_analysis PRIVATE src/analysis/lexical.cc)
target_sources(abcd_analysis PRIVATE src/analysis/environment_index.cc)
target_sources(abcd_analysis PRIVATE src/analysis/environment_flow.cc)
target_sources(abcd_analysis PRIVATE src/analysis/hole_guard.cc)
target_sources(abcd_analysis PRIVATE src/analysis/indexed_read.cc)
target_sources(abcd_analysis PRIVATE src/analysis/indexed_write.cc)
target_sources(abcd_analysis PRIVATE src/analysis/named_read.cc)
target_sources(abcd_analysis PRIVATE src/analysis/named_write.cc)
target_sources(abcd_analysis PRIVATE src/analysis/own_write.cc)
target_sources(abcd_analysis PRIVATE src/analysis/value_write.cc)
target_sources(abcd_analysis PRIVATE src/analysis/property_write.cc)
target_sources(abcd_analysis PRIVATE src/analysis/property_receiver.cc)
target_sources(abcd_analysis PRIVATE src/analysis/global_access.cc)
target_sources(abcd_analysis PRIVATE src/analysis/module_load.cc)
target_sources(abcd_analysis PRIVATE src/analysis/module_store.cc)
target_sources(abcd_analysis PRIVATE src/analysis/ordinary_call.cc)
target_sources(abcd_analysis PRIVATE src/analysis/constructor_call.cc)
target_sources(abcd_analysis PRIVATE src/analysis/named_invocation.cc)
target_sources(abcd_analysis PRIVATE src/analysis/phi_placement.cc src/analysis/iterated_frontier.cc)
target_sources(abcd_analysis PRIVATE src/analysis/binding_liveness.cc)
target_sources(abcd_analysis PRIVATE src/analysis/register_effect.cc)
target_sources(abcd_analysis PRIVATE src/analysis/register_uses.cc)
target_sources(abcd_analysis PRIVATE src/analysis/register_ssa.cc src/analysis/register_rename.cc)
target_sources(abcd_analysis PRIVATE src/analysis/register_value_flow.cc src/analysis/register_value_rule.cc src/analysis/lexical_value_index.cc)
target_sources(abcd_analysis PRIVATE src/analysis/object_creation.cc)
target_sources(abcd_analysis PRIVATE src/analysis/array_creation.cc)
target_sources(abcd_analysis PRIVATE src/analysis/guarded_reads.cc)
target_sources(abcd_analysis PRIVATE src/analysis/environment_escape.cc src/analysis/environment_escape_aliases.cc)
target_sources(abcd_analysis PRIVATE src/analysis/linear_lexicals.cc)
target_sources(abcd_analysis PRIVATE src/analysis/block_order.cc)
target_sources(abcd_analysis PRIVATE src/analysis/accumulator_flow.cc src/analysis/scalar_access.cc)
target_link_libraries(abcd_analysis PUBLIC abcd_abc)
add_library(abcd_source src/source/string_literal.cc src/source/scalar_expression.cc)
target_sources(abcd_source PRIVATE src/source/global_reference.cc)
target_sources(abcd_source PRIVATE src/source/parameter_names.cc)
target_sources(abcd_source PRIVATE src/source/constructor_fields.cc)
target_sources(abcd_source PRIVATE src/source/parameter_bindings.cc)
target_sources(abcd_source PRIVATE src/source/linear_body.cc)
target_sources(abcd_source PRIVATE src/source/fresh_object_fields.cc)
target_sources(abcd_source PRIVATE src/source/object_buffer.cc)
target_sources(abcd_source PRIVATE src/source/acyclic_body.cc)
target_sources(abcd_source PRIVATE src/source/local_lexical_body.cc)
target_sources(abcd_source PRIVATE src/source/named_scope.cc)
target_sources(abcd_source PRIVATE src/source/module_binding.cc)
target_sources(abcd_source PRIVATE src/source/module_imports.cc)
target_sources(abcd_source PRIVATE src/source/import_names.cc)
target_sources(abcd_source PRIVATE src/source/module_exports.cc)
target_sources(abcd_source PRIVATE src/source/record_program.cc src/source/record_source.cc)
target_sources(abcd_source PRIVATE src/source/capture_preservation.cc)
target_sources(abcd_source PRIVATE src/source/defined_captures.cc)
target_sources(abcd_source PRIVATE src/source/record_lexical.cc)
target_sources(abcd_source PRIVATE src/source/record_module_choice.cc)
target_sources(abcd_source PRIVATE src/source/record_prelude.cc)
target_sources(abcd_source PRIVATE src/source/record_literal.cc)
target_sources(abcd_source PRIVATE src/source/record_array.cc)
target_sources(abcd_source PRIVATE src/source/record_buffer.cc)
target_sources(abcd_source PRIVATE src/source/static_fields.cc)
target_sources(abcd_source PRIVATE src/source/record_module_context.cc)
target_sources(abcd_source PRIVATE src/source/base_function.cc)
target_sources(abcd_source PRIVATE src/source/argument_region.cc src/source/semantic_ast.cc src/source/semantic_printer.cc
  src/source/semantic_body.cc src/source/semantic_entry.cc src/source/semantic_fields.cc
  src/source/semantic_objects.cc src/source/copy_origins.cc src/source/record_inputs.cc
  src/source/dead_copies.cc src/source/array_literals.cc src/source/await_positions.cc
  src/source/return_expression.cc
  src/source/stored_expression.cc
  src/source/stored_calls.cc
  src/source/object_initialization.cc
  src/source/store_receivers.cc
  src/source/private_copy.cc
  src/source/predicate_copies.cc
  src/source/conditional_value.cc
  src/source/string_comparison.cc
  src/source/branch_predicate.cc
  src/source/scalar_values.cc
  src/source/parameter_aliases.cc
  src/source/entry_bindings.cc
  src/source/entry_copy_elision.cc
  src/source/argument_expression.cc
  src/source/super_arguments.cc
  src/source/constructor_arguments.cc
  src/source/class_parent_read.cc
  src/source/overwritten_accumulator.cc
  src/source/parameter_defaults.cc
  src/source/receiver_binding.cc
  src/source/register_reads.cc
  src/source/store_snapshots.cc
  src/source/slot_liveness.cc
  src/source/field_reads.cc
  src/source/member_predicates.cc
  src/source/result_copies.cc
  src/source/rest_binding.cc
  src/source/unused_results.cc
  src/source/binding_receivers.cc
  src/source/class_assembly.cc src/source/assembled_class.cc)
target_sources(abcd_source PRIVATE src/source/async_completion.cc src/source/async_handlers.cc)
target_sources(abcd_source PRIVATE src/source/exception_source.cc)
target_sources(abcd_source PRIVATE src/source/exception_body.cc)
target_sources(abcd_source PRIVATE src/source/module_read_snapshot.cc)
target_sources(abcd_source PRIVATE src/source/branch_value_proof.cc)
target_sources(abcd_source PRIVATE src/source/branch_source_facts.cc)
target_sources(abcd_source PRIVATE src/source/staged_branch_call.cc)
target_sources(abcd_source PRIVATE src/source/branch_staged_calls.cc)
target_sources(abcd_source PRIVATE src/source/structured_catch.cc)
target_sources(abcd_source PRIVATE src/source/catch_register_liveness.cc)
target_sources(abcd_source PRIVATE src/source/created_function.cc)
target_sources(abcd_source PRIVATE src/source/created_class.cc
  src/source/class_members.cc src/source/class_source.cc)
target_sources(abcd_source PRIVATE src/source/record_declarations.cc)
target_sources(abcd_source PRIVATE src/source/spread_field_constructor.cc)
target_sources(abcd_source PRIVATE src/source/leaf_creation.cc)
target_sources(abcd_source PRIVATE src/source/leaf_sources.cc
  src/source/semantic_binding.cc src/source/semantic_capture.cc)
target_sources(abcd_source PRIVATE src/source/source_route.cc)
target_sources(abcd_source PRIVATE src/source/value_stage.cc)
target_sources(abcd_source PRIVATE src/source/control_plan.cc src/source/control_verify.cc)
target_sources(abcd_source PRIVATE src/source/source_tree.cc)
target_sources(abcd_source PRIVATE src/source/evaluation_shape.cc src/source/evaluation_plan.cc src/source/evaluation_verify.cc src/source/binding_plan.cc)
target_sources(abcd_source PRIVATE src/source/expression_forest.cc src/source/function_tree.cc)
target_sources(abcd_source PRIVATE src/source/tree_structure.cc src/source/tree_scope.cc src/source/tree_origins.cc)
target_sources(abcd_source PRIVATE src/source/tree_flow.cc)
target_sources(abcd_source PRIVATE src/source/tree_operands.cc src/source/tree_correspondence_origins.cc src/source/tree_semantics.cc src/source/tree_globals.cc)
target_sources(abcd_source PRIVATE src/source/edge_copies.cc src/source/edge_copy_verify.cc)
target_sources(abcd_source PRIVATE src/source/phi_copies.cc src/source/phi_copy_verify.cc)
target_sources(abcd_source PRIVATE src/source/tree_edge_flow.cc)
target_sources(abcd_source PRIVATE src/source/loop_forest.cc)
target_sources(abcd_source PRIVATE src/source/loop_control.cc)
target_sources(abcd_source PRIVATE src/source/literal_plan.cc)
target_sources(abcd_source PRIVATE src/source/scope_facts.cc src/source/capture_plan.cc)
target_sources(abcd_source PRIVATE src/source/tree_constants.cc)
target_sources(abcd_source PRIVATE src/source/class_tree_context.cc)
target_sources(abcd_source PRIVATE src/source/verified_function.cc)
target_sources(abcd_source PRIVATE src/source/function_abi.cc)
target_sources(abcd_source PRIVATE src/source/class_method_emission.cc)
target_sources(abcd_source PRIVATE src/source/function_segments.cc)
target_sources(abcd_source PRIVATE src/source/selected_class_method.cc)
target_sources(abcd_source PRIVATE src/source/parameter_plan.cc)
target_sources(abcd_source PRIVATE src/source/default_candidates.cc)
target_sources(abcd_source PRIVATE src/source/default_value_proof.cc)
target_sources(abcd_source PRIVATE src/source/default_body.cc)
target_sources(abcd_source PRIVATE src/source/call_plan.cc)
target_sources(abcd_source PRIVATE src/source/default_signature.cc src/source/default_signature_verify.cc)
target_sources(abcd_source PRIVATE src/source/tree_printer.cc src/source/tree_print_tokens.cc)
target_sources(abcd_source PRIVATE src/source/evaluation_boolean.cc)
target_link_libraries(abcd_source PUBLIC abcd_analysis PRIVATE abcd_json)
target_compile_features(abcd_source PUBLIC cxx_std_20)
target_include_directories(abcd_source PUBLIC include)
add_library(abcd_structure src/structure/record_path.cc src/structure/record_writer.cc
  src/structure/module_writer.cc src/structure/source_identity.cc
  src/structure/package_source.cc)
target_sources(abcd_structure PRIVATE src/structure/debug_writer.cc)
target_sources(abcd_structure PRIVATE src/structure/scope_writer.cc)
target_sources(abcd_structure PRIVATE src/structure/class_writer.cc)
target_sources(abcd_structure PRIVATE src/structure/property_writer.cc)
target_sources(abcd_structure PRIVATE src/structure/global_writer.cc)
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
  add_executable(abc_semantic tools/abc_semantic.cc tools/semantic_survey.cc
    tools/exception_inspect.cc tools/exception_source_inspect.cc tools/record_module.cc
    tools/value_source_inspect.cc)
  target_link_libraries(abc_semantic PRIVATE abcd_source abcd_json)
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
  add_executable(iterator_candidates EXCLUDE_FROM_ALL
    tests/tools/iterator_candidates.cc)
  target_link_libraries(iterator_candidates PRIVATE abcd_analysis abcd_corpus
    abcd_json)
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
  add_executable(class_buffer_test tests/unit/abc/class_buffer_test.cc)
  target_link_libraries(class_buffer_test PRIVATE abcd_abc)
  add_test(NAME class_buffer_test COMMAND class_buffer_test)
  add_executable(class_creation_test tests/unit/analysis/class_creation_test.cc)
  target_link_libraries(class_creation_test PRIVATE abcd_analysis)
  add_test(NAME class_creation_test COMMAND class_creation_test)
  add_executable(rest_arguments_test tests/unit/analysis/rest_arguments_test.cc)
  target_include_directories(rest_arguments_test PRIVATE tests/support)
  target_link_libraries(rest_arguments_test PRIVATE abcd_analysis)
  add_test(NAME rest_arguments_test COMMAND rest_arguments_test)
  add_executable(class_heritage_test tests/unit/analysis/class_heritage_test.cc)
  target_include_directories(class_heritage_test PRIVATE tests/support)
  target_link_libraries(class_heritage_test PRIVATE abcd_analysis)
  add_test(NAME class_heritage_test COMMAND class_heritage_test)
  add_executable(method_roles_test tests/unit/analysis/method_roles_test.cc)
  target_include_directories(method_roles_test PRIVATE tests/support)
  target_link_libraries(method_roles_test PRIVATE abcd_analysis)
  add_test(NAME method_roles_test COMMAND method_roles_test)
  add_executable(own_write_test tests/unit/analysis/own_write_test.cc)
  add_executable(property_write_test tests/unit/analysis/property_write_test.cc)
  target_link_libraries(property_write_test PRIVATE abcd_analysis)
  add_test(NAME property_write_test COMMAND property_write_test)
  add_executable(global_access_test tests/unit/analysis/global_access_test.cc)
  add_executable(module_load_test tests/unit/analysis/module_load_test.cc)
  add_executable(module_store_test tests/unit/analysis/module_store_test.cc)
  target_link_libraries(module_store_test PRIVATE abcd_analysis)
  add_test(NAME module_store_test COMMAND module_store_test)
  target_link_libraries(module_load_test PRIVATE abcd_analysis)
  add_test(NAME module_load_test COMMAND module_load_test)
  target_link_libraries(global_access_test PRIVATE abcd_analysis)
  add_test(NAME global_access_test COMMAND global_access_test)
  add_executable(property_receiver_test tests/unit/analysis/property_receiver_test.cc)
  target_link_libraries(property_receiver_test PRIVATE abcd_analysis)
  add_test(NAME property_receiver_test COMMAND property_receiver_test)
  target_link_libraries(own_write_test PRIVATE abcd_analysis)
  add_test(NAME own_write_test COMMAND own_write_test)
  add_executable(class_writer_test tests/unit/structure/class_writer_test.cc)
  target_include_directories(class_writer_test PRIVATE src/structure)
  target_link_libraries(class_writer_test PRIVATE abcd_structure abcd_json)
  add_test(NAME class_writer_test COMMAND class_writer_test)
  add_executable(derived_constructor_test
    tests/unit/analysis/derived_constructor_test.cc)
  target_link_libraries(derived_constructor_test PRIVATE abcd_analysis)
  add_test(NAME derived_constructor_test COMMAND derived_constructor_test)
  if(EXISTS "${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc")
    add_executable(derived_constructor_sample_test
      tests/integration/derived_constructor_sample.cc)
    target_link_libraries(derived_constructor_sample_test PRIVATE abcd_analysis)
    add_test(NAME derived_constructor_sample_test
      COMMAND derived_constructor_sample_test
        "${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc")
  endif()
  if(EXISTS "${ABCD_PAIRED_CORPUS_ROOT}/pasteboard/modules.abc")
    add_executable(spread_field_constructor_sample_test
      tests/integration/spread_field_constructor_sample.cc)
    target_include_directories(spread_field_constructor_sample_test
      PRIVATE src/source)
    target_link_libraries(spread_field_constructor_sample_test
      PRIVATE abcd_source)
    add_test(NAME spread_field_constructor_sample_test
      COMMAND spread_field_constructor_sample_test
        "${ABCD_PAIRED_CORPUS_ROOT}/pasteboard/modules.abc")
    add_executable(historical_async_member_sample_test
      tests/integration/historical_async_member_sample.cc)
    target_include_directories(historical_async_member_sample_test
      PRIVATE src/source)
    target_link_libraries(historical_async_member_sample_test
      PRIVATE abcd_source)
    add_test(NAME historical_async_member_sample_test
      COMMAND historical_async_member_sample_test
        "${ABCD_PAIRED_CORPUS_ROOT}/pasteboard/modules.abc")
    if(ABCD_BUILD_TOOLS)
      # Route admission CLI regressions (T00 corrections R1/R5/R6):
      # specification only, NOT executed during the user's test pause.
      add_test(NAME source_route_cli_test COMMAND ${CMAKE_COMMAND}
        "-DABCD_SEMANTIC=$<TARGET_FILE:abc_semantic>"
        "-DABCD_STRUCTURE=$<TARGET_FILE:abc_structure>"
        "-DABCD_SAMPLE=${ABCD_PAIRED_CORPUS_ROOT}/pasteboard/modules.abc"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/source_route_cli.cmake)
    endif()
  endif()
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
  add_executable(source_route_test tests/unit/source/source_route_test.cc)
  add_executable(value_operands_test tests/unit/analysis/value_operands_test.cc)
  target_include_directories(value_operands_test PRIVATE src/analysis)
  target_link_libraries(value_operands_test PRIVATE abcd_analysis)
  add_test(NAME value_operands_test COMMAND value_operands_test)
  add_executable(call_operands_test tests/unit/analysis/call_operands_test.cc)
  target_include_directories(call_operands_test PRIVATE src/analysis)
  target_link_libraries(call_operands_test PRIVATE abcd_analysis)
  add_test(NAME call_operands_test COMMAND call_operands_test)
  add_executable(value_uses_test tests/unit/analysis/value_uses_test.cc)
  target_link_libraries(value_uses_test PRIVATE abcd_analysis)
  add_test(NAME value_uses_test COMMAND value_uses_test)
  add_executable(value_import_test tests/unit/analysis/value_import_test.cc)
  add_executable(value_simplify_test tests/unit/analysis/value_simplify_test.cc)
  add_executable(value_flow_test tests/unit/analysis/value_flow_test.cc)
  add_executable(value_phi_test tests/unit/analysis/value_phi_test.cc)
  add_executable(control_plan_test tests/unit/source/control_plan_test.cc)
  add_executable(tree_constants_test tests/unit/source/tree_constants_test.cc)
  target_link_libraries(tree_constants_test PRIVATE abcd_source)
  add_test(NAME tree_constants_test COMMAND tree_constants_test)
  add_executable(class_tree_context_test tests/unit/source/class_tree_context_test.cc)
  target_link_libraries(class_tree_context_test PRIVATE abcd_source)
  add_test(NAME class_tree_context_test COMMAND class_tree_context_test)
  add_executable(tree_structure_test tests/unit/source/tree_structure_test.cc)
  target_link_libraries(tree_structure_test PRIVATE abcd_source)
  add_test(NAME tree_structure_test COMMAND tree_structure_test)
  add_executable(function_tree_test tests/unit/source/function_tree_test.cc)
  target_link_libraries(function_tree_test PRIVATE abcd_source)
  add_test(NAME function_tree_test COMMAND function_tree_test)
  add_executable(binding_plan_test tests/unit/source/binding_plan_test.cc)
  target_link_libraries(binding_plan_test PRIVATE abcd_source)
  add_test(NAME binding_plan_test COMMAND binding_plan_test)
  add_executable(parameter_plan_test tests/unit/source/parameter_plan_test.cc)
  target_link_libraries(parameter_plan_test PRIVATE abcd_source)
  add_test(NAME parameter_plan_test COMMAND parameter_plan_test)
  add_executable(default_candidates_test tests/unit/source/default_candidates_test.cc)
  target_link_libraries(default_candidates_test PRIVATE abcd_source)
  add_test(NAME default_candidates_test COMMAND default_candidates_test)
  add_executable(evaluation_plan_test tests/unit/source/evaluation_plan_test.cc)
  target_link_libraries(evaluation_plan_test PRIVATE abcd_source)
  add_test(NAME evaluation_plan_test COMMAND evaluation_plan_test)
  add_executable(call_plan_test tests/unit/source/call_plan_test.cc)
  target_link_libraries(call_plan_test PRIVATE abcd_source)
  add_test(NAME call_plan_test COMMAND call_plan_test)
  add_executable(tree_globals_test tests/unit/source/tree_globals_test.cc)
  target_link_libraries(tree_globals_test PRIVATE abcd_source)
  add_test(NAME tree_globals_test COMMAND tree_globals_test)
  add_executable(edge_copies_test tests/unit/source/edge_copies_test.cc)
  target_link_libraries(edge_copies_test PRIVATE abcd_source)
  add_test(NAME edge_copies_test COMMAND edge_copies_test)
  add_executable(phi_copies_test tests/unit/source/phi_copies_test.cc)
  target_link_libraries(phi_copies_test PRIVATE abcd_source)
  add_test(NAME phi_copies_test COMMAND phi_copies_test)
  add_executable(phi_call_test tests/unit/source/phi_call_test.cc)
  target_link_libraries(phi_call_test PRIVATE abcd_source)
  add_test(NAME phi_call_test COMMAND phi_call_test)
  add_executable(loop_forest_test tests/unit/source/loop_forest_test.cc)
  add_executable(literal_plan_test tests/unit/source/literal_plan_test.cc)
  target_link_libraries(literal_plan_test PRIVATE abcd_source)
  add_test(NAME literal_plan_test COMMAND literal_plan_test)
  add_executable(capture_plan_test tests/unit/source/capture_plan_test.cc)
  target_link_libraries(capture_plan_test PRIVATE abcd_source)
  add_test(NAME capture_plan_test COMMAND capture_plan_test)
  target_link_libraries(loop_forest_test PRIVATE abcd_source)
  add_test(NAME loop_forest_test COMMAND loop_forest_test)
  target_link_libraries(control_plan_test PRIVATE abcd_source)
  add_test(NAME control_plan_test COMMAND control_plan_test)
  target_link_libraries(value_phi_test PRIVATE abcd_analysis)
  add_test(NAME value_phi_test COMMAND value_phi_test)
  target_link_libraries(value_flow_test PRIVATE abcd_analysis)
  add_test(NAME value_flow_test COMMAND value_flow_test)
  target_link_libraries(value_simplify_test PRIVATE abcd_analysis)
  add_test(NAME value_simplify_test COMMAND value_simplify_test)
  target_include_directories(value_import_test PRIVATE tests/support)
  target_link_libraries(value_import_test PRIVATE abcd_analysis)
  add_test(NAME value_import_test COMMAND value_import_test)
  add_executable(value_call_import_test tests/unit/analysis/value_call_import_test.cc)
  target_include_directories(value_call_import_test PRIVATE src/analysis tests/support)
  target_link_libraries(value_call_import_test PRIVATE abcd_analysis)
  add_test(NAME value_call_import_test COMMAND value_call_import_test)
  add_executable(value_creation_import_test tests/unit/analysis/value_creation_import_test.cc)
  target_include_directories(value_creation_import_test PRIVATE src/analysis tests/support)
  target_link_libraries(value_creation_import_test PRIVATE abcd_analysis)
  add_test(NAME value_creation_import_test COMMAND value_creation_import_test)
  add_executable(value_global_import_test tests/unit/analysis/value_global_import_test.cc)
  target_include_directories(value_global_import_test PRIVATE src/analysis tests/support)
  target_link_libraries(value_global_import_test PRIVATE abcd_analysis)
  add_test(NAME value_global_import_test COMMAND value_global_import_test)
  target_link_libraries(source_route_test PRIVATE abcd_source abcd_json)
  add_test(NAME source_route_test COMMAND source_route_test)
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
  add_executable(constructor_call_test tests/unit/analysis/constructor_call_test.cc)
  target_link_libraries(constructor_call_test PRIVATE abcd_source)
  add_test(NAME constructor_call_test COMMAND constructor_call_test)
  add_executable(indexed_read_test tests/unit/analysis/indexed_read_test.cc)
  target_link_libraries(indexed_read_test PRIVATE abcd_analysis)
  add_test(NAME indexed_read_test COMMAND indexed_read_test)
  add_executable(indexed_write_test tests/unit/analysis/indexed_write_test.cc)
  target_link_libraries(indexed_write_test PRIVATE abcd_analysis)
  add_test(NAME indexed_write_test COMMAND indexed_write_test)
  add_executable(hole_guard_test tests/unit/analysis/hole_guard_test.cc)
  target_link_libraries(hole_guard_test PRIVATE abcd_analysis)
  add_test(NAME hole_guard_test COMMAND hole_guard_test)
  add_executable(lexical_test tests/unit/analysis/lexical_test.cc)
  target_link_libraries(lexical_test PRIVATE abcd_analysis)
  add_test(NAME lexical_test COMMAND lexical_test)
  add_executable(function_creation_test tests/unit/analysis/function_creation_test.cc)
  target_link_libraries(function_creation_test PRIVATE abcd_analysis)
  add_test(NAME function_creation_test COMMAND function_creation_test)
  add_executable(define_instruction_test tests/unit/analysis/define_instruction_test.cc)
  target_link_libraries(define_instruction_test PRIVATE abcd_analysis)
  add_test(NAME define_instruction_test COMMAND define_instruction_test)
  add_executable(define_computed_fields_test
    tests/unit/source/define_computed_fields_test.cc)
  target_link_libraries(define_computed_fields_test PRIVATE abcd_source)
  add_test(NAME define_computed_fields_test COMMAND define_computed_fields_test)
  add_executable(method_attributes_test tests/unit/abc/method_attributes_test.cc)
  target_link_libraries(method_attributes_test PRIVATE abcd_abc)
  add_test(NAME method_attributes_test COMMAND method_attributes_test)
  add_executable(linear_constants_test tests/unit/source/linear_constants_test.cc)
  target_link_libraries(linear_constants_test PRIVATE abcd_source)
  add_test(NAME linear_constants_test COMMAND linear_constants_test)
  add_executable(parameter_bindings_test
    tests/unit/source/parameter_bindings_test.cc)
  target_include_directories(parameter_bindings_test PRIVATE src/source tests/support)
  target_link_libraries(parameter_bindings_test PRIVATE abcd_source)
  add_test(NAME parameter_bindings_test COMMAND parameter_bindings_test)
  add_executable(object_buffer_test tests/unit/source/object_buffer_test.cc)
  target_include_directories(object_buffer_test PRIVATE tests/support src/source)
  target_link_libraries(object_buffer_test PRIVATE abcd_source)
  add_test(NAME object_buffer_test COMMAND object_buffer_test)
  add_executable(named_call_test tests/unit/source/named_call_test.cc)
  target_include_directories(named_call_test PRIVATE tests/support)
  target_link_libraries(named_call_test PRIVATE abcd_source)
  add_test(NAME named_call_test COMMAND named_call_test)
  add_executable(linear_body_test tests/unit/source/linear_body_test.cc)
  target_link_libraries(linear_body_test PRIVATE abcd_source)
  add_test(NAME linear_body_test COMMAND linear_body_test)
  add_executable(base_function_test tests/unit/source/base_function_test.cc)
  add_executable(base_constructor_test tests/unit/source/base_constructor_test.cc)
  target_include_directories(base_constructor_test PRIVATE tests/support)
  target_link_libraries(base_constructor_test PRIVATE abcd_source)
  add_test(NAME base_constructor_test COMMAND base_constructor_test)
  add_executable(class_method_test tests/unit/source/class_method_test.cc)
  target_include_directories(class_method_test PRIVATE tests/support)
  target_link_libraries(class_method_test PRIVATE abcd_source)
  add_test(NAME class_method_test COMMAND class_method_test)
  add_executable(class_accessor_test tests/unit/source/class_accessor_test.cc)
  target_include_directories(class_accessor_test PRIVATE tests/support)
  target_link_libraries(class_accessor_test PRIVATE abcd_source)
  add_test(NAME class_accessor_test COMMAND class_accessor_test)
  target_include_directories(base_function_test PRIVATE tests/support src/source)
  target_link_libraries(base_function_test PRIVATE abcd_source)
  add_test(NAME base_function_test COMMAND base_function_test)
  add_executable(created_function_test tests/unit/source/created_function_test.cc tests/support/source/leaf_function_checks.cc)
  target_include_directories(created_function_test PRIVATE tests/support src/source)
  target_link_libraries(created_function_test PRIVATE abcd_source)
  add_test(NAME created_function_test COMMAND created_function_test)
  add_executable(created_class_test tests/unit/source/created_class_test.cc)
  target_include_directories(created_class_test PRIVATE tests/support)
  target_link_libraries(created_class_test PRIVATE abcd_source)
  add_test(NAME created_class_test COMMAND created_class_test)
  add_executable(derived_class_test tests/unit/source/derived_class_test.cc)
  target_include_directories(derived_class_test PRIVATE tests/support)
  target_link_libraries(derived_class_test PRIVATE abcd_source)
  add_test(NAME derived_class_test COMMAND derived_class_test)
  add_executable(spread_field_constructor_test
    tests/unit/source/spread_field_constructor_test.cc)
  target_include_directories(spread_field_constructor_test PRIVATE src/source)
  target_link_libraries(spread_field_constructor_test PRIVATE abcd_source)
  add_test(NAME spread_field_constructor_test
    COMMAND spread_field_constructor_test)
  add_executable(created_members_test tests/unit/source/created_members_test.cc)
  target_include_directories(created_members_test PRIVATE tests/support)
  target_link_libraries(created_members_test PRIVATE abcd_source)
  add_test(NAME created_members_test COMMAND created_members_test)
  add_executable(created_members_scale_test tests/unit/source/created_members_scale_test.cc)
  target_include_directories(created_members_scale_test PRIVATE tests/support)
  target_link_libraries(created_members_scale_test PRIVATE abcd_source)
  add_test(NAME created_members_scale_test COMMAND created_members_scale_test)
  add_executable(class_sample_fixture tests/integration/class_sample.cc)
  target_include_directories(class_sample_fixture PRIVATE tests/support)
  target_link_libraries(class_sample_fixture PRIVATE abcd_source abcd_corpus)
  add_executable(local_lexical_body_test tests/unit/source/local_lexical_body_test.cc tests/support/analysis/local_lexical_scaling.cc)
  target_include_directories(local_lexical_body_test PRIVATE tests/support)
  target_link_libraries(local_lexical_body_test PRIVATE abcd_source)
  add_test(NAME local_lexical_body_test COMMAND local_lexical_body_test)
  add_executable(named_scope_test tests/unit/source/named_scope_test.cc)
  target_include_directories(named_scope_test PRIVATE tests/support)
  target_link_libraries(named_scope_test PRIVATE abcd_source)
  add_test(NAME named_scope_test COMMAND named_scope_test)
  add_executable(module_import_body_test
    tests/unit/source/module_import_body_test.cc)
  target_include_directories(module_import_body_test PRIVATE tests/support src/source)
  target_link_libraries(module_import_body_test PRIVATE abcd_source)
  add_test(NAME module_import_body_test COMMAND module_import_body_test)
  add_executable(module_exports_test tests/unit/source/module_exports_test.cc)
  target_link_libraries(module_exports_test PRIVATE abcd_source)
  add_test(NAME module_exports_test COMMAND module_exports_test)
  add_executable(record_module_context_test
    tests/unit/source/record_module_context_test.cc)
  target_link_libraries(record_module_context_test PRIVATE abcd_source)
  add_test(NAME record_module_context_test COMMAND record_module_context_test)
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
  set(ABCD_PAIRED_CORPUS_ROOT "" CACHE PATH "Paired ABC sample root")
  find_program(ABCD_NODE_EXECUTABLE node)
  if(ABCD_NODE_EXECUTABLE AND ABCD_BUILD_TOOLS)
    if(EXISTS "${ABCD_PAIRED_CORPUS_ROOT}/pasteboard/modules.abc")
      add_test(NAME value_tree_pasteboard_slice_test COMMAND ${ABCD_NODE_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/value_tree_slice.mjs
        $<TARGET_FILE:abc_semantic> "${ABCD_PAIRED_CORPUS_ROOT}/pasteboard/modules.abc"
        7586 854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e pathStack)
      add_test(NAME value_tree_record_slice_test COMMAND ${ABCD_NODE_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/value_record_slice.mjs
        $<TARGET_FILE:abc_semantic> "${ABCD_PAIRED_CORPUS_ROOT}/pasteboard/modules.abc"
        7586 854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e)
      add_test(NAME value_tree_call_slice_test COMMAND ${ABCD_NODE_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/value_call_slice.mjs
        $<TARGET_FILE:abc_semantic> "${ABCD_PAIRED_CORPUS_ROOT}/pasteboard/modules.abc")
      add_test(NAME value_tree_global_call_slice_test COMMAND ${ABCD_NODE_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/value_global_call_slice.mjs
        $<TARGET_FILE:abc_semantic> "${ABCD_PAIRED_CORPUS_ROOT}/pasteboard/modules.abc")
    endif()
    if(EXISTS "${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc")
      add_test(NAME value_tree_immersive_slice_test COMMAND ${ABCD_NODE_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/value_tree_slice.mjs
        $<TARGET_FILE:abc_semantic> "${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc"
        21082 8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a buttonText)
    endif()
  endif()
  if(EXISTS "${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc")
    add_executable(define_static_sample_test
      tests/integration/define_static_sample.cc)
    target_link_libraries(define_static_sample_test PRIVATE abcd_source)
    add_test(NAME define_static_sample_test COMMAND define_static_sample_test
      "${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc")
    if(EXISTS "${ABCD_PAIRED_CORPUS_ROOT}/pasteboard/modules.abc")
      add_test(NAME historical_static_sample_test
        COMMAND define_static_sample_test
          "${ABCD_PAIRED_CORPUS_ROOT}/pasteboard/modules.abc")
    endif()
    add_executable(module_exports_sample_test
      tests/integration/module_exports_sample.cc)
    target_link_libraries(module_exports_sample_test PRIVATE abcd_source)
    add_test(NAME module_exports_sample_test COMMAND module_exports_sample_test
      "${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc")
    if(ABCD_NODE_EXECUTABLE)
      if(ABCD_BUILD_TOOLS)
        add_test(NAME constructor_record_sample_test COMMAND ${CMAKE_COMMAND}
          "-DABCD_STRUCTURE=$<TARGET_FILE:abc_structure>"
          "-DABCD_SAMPLE=${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc"
          "-DABCD_NODE=${ABCD_NODE_EXECUTABLE}"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/constructor_record.cmake)
      endif()
      add_test(NAME record_program_sample_js_test COMMAND ${CMAKE_COMMAND}
        "-DABCD_FIXTURE=$<TARGET_FILE:module_exports_sample_test>"
        "-DABCD_PAIRED_SAMPLE=${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc"
        "-DABCD_NODE=${ABCD_NODE_EXECUTABLE}"
        "-DABCD_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_program_sample.mjs"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_program_sample.cmake)
    endif()
  endif()
  add_executable(record_function_test
    tests/unit/source/record_function_test.cc)
  target_link_libraries(record_function_test PRIVATE abcd_source)
  add_test(NAME record_function_test COMMAND record_function_test)
  add_executable(record_accessor_test
    tests/unit/source/record_accessor_test.cc)
  target_link_libraries(record_accessor_test PRIVATE abcd_source)
  add_test(NAME record_accessor_test COMMAND record_accessor_test)
  add_executable(record_object_test
    tests/unit/source/record_object_test.cc)
  target_link_libraries(record_object_test PRIVATE abcd_source)
  add_test(NAME record_object_test COMMAND record_object_test)
  add_executable(record_import_test
    tests/unit/source/record_import_test.cc)
  target_link_libraries(record_import_test PRIVATE abcd_source)
  add_test(NAME record_import_test COMMAND record_import_test)
  add_executable(record_literal_test
    tests/unit/source/record_literal_test.cc)
  target_include_directories(record_literal_test PRIVATE src/source)
  target_link_libraries(record_literal_test PRIVATE abcd_source)
  add_test(NAME record_literal_test COMMAND record_literal_test)
  if(ABCD_NODE_EXECUTABLE)
    add_test(NAME record_import_js_test COMMAND ${CMAKE_COMMAND}
      "-DABCD_FIXTURE=$<TARGET_FILE:record_import_test>"
      "-DABCD_NODE=${ABCD_NODE_EXECUTABLE}"
      "-DABCD_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_import.mjs"
      "-DABCD_DEPENDENCY=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_import_dependency.mjs"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_import.cmake)
    add_test(NAME record_object_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:record_object_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_object.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=record-object.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    add_test(NAME record_accessor_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:record_accessor_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_accessor.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=record-accessor.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    add_test(NAME record_function_js_test COMMAND ${CMAKE_COMMAND}
      "-DABCD_FIXTURE=$<TARGET_FILE:record_function_test>"
      "-DABCD_NODE=${ABCD_NODE_EXECUTABLE}"
      "-DABCD_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_function.mjs"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_function.cmake)
    add_test(NAME record_function_construct_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_FIXTURE=$<TARGET_FILE:record_function_test>
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_function_construct.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_function_construct.cmake)
  endif()
  if(ABCD_NODE_EXECUTABLE)
    if(EXISTS "${ABCD_PAIRED_CORPUS_ROOT}/MultiCommunityApplication/modules.abc")
      add_test(NAME paired_record_import_class_js_test COMMAND ${CMAKE_COMMAND}
        "-DABCD_CLASS_FIXTURE=$<TARGET_FILE:class_sample_fixture>"
        "-DABCD_PAIRED_SAMPLE=${ABCD_PAIRED_CORPUS_ROOT}/MultiCommunityApplication/modules.abc"
        "-DABCD_RECORD=44106"
        "-DABCD_RECORD_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_import_class.mjs"
        "-DABCD_NODE=${ABCD_NODE_EXECUTABLE}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/paired_record_import_class.cmake)
    endif()
    if(EXISTS "${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc")
      add_test(NAME paired_home_model_record_source_test COMMAND ${CMAKE_COMMAND}
        "-DABCD_STRUCTURE=$<TARGET_FILE:abc_structure>"
        "-DABCD_SAMPLE=${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc"
        "-DABCD_NODE=${ABCD_NODE_EXECUTABLE}"
        "-DABCD_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_home_model.mjs"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_home_model.cmake)
      add_test(NAME paired_model_record_source_test COMMAND ${CMAKE_COMMAND}
        "-DABCD_STRUCTURE=$<TARGET_FILE:abc_structure>"
        "-DABCD_SAMPLE=${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc"
        "-DABCD_NODE=${ABCD_NODE_EXECUTABLE}"
        "-DABCD_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_model_calls.mjs"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_model_calls.cmake)
      add_test(NAME paired_viewmodel_record_source_test COMMAND ${CMAKE_COMMAND}
        "-DABCD_STRUCTURE=$<TARGET_FILE:abc_structure>"
        "-DABCD_SAMPLE=${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc"
        "-DABCD_NODE=${ABCD_NODE_EXECUTABLE}"
        "-DABCD_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_viewmodels.mjs"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_viewmodels.cmake)
      add_test(NAME paired_home_model_js_test COMMAND ${CMAKE_COMMAND}
        "-DABCD_CLASS_FIXTURE=$<TARGET_FILE:class_sample_fixture>"
        "-DABCD_PAIRED_SAMPLE=${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc"
        "-DABCD_RECORD=16474"
        "-DABCD_RECORD_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/home_model.mjs"
        "-DABCD_NODE=${ABCD_NODE_EXECUTABLE}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/paired_record_import_class.cmake)
      add_test(NAME paired_breakpoint_type_js_test COMMAND ${CMAKE_COMMAND}
        "-DABCD_CLASS_FIXTURE=$<TARGET_FILE:class_sample_fixture>"
        "-DABCD_PAIRED_SAMPLE=${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc"
        "-DABCD_RECORD=7395"
        "-DABCD_RECORD_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/breakpoint_type.mjs"
        "-DABCD_NODE=${ABCD_NODE_EXECUTABLE}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/paired_record_import_class.cmake)
    endif()
    add_test(NAME module_import_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_IMPORT_FIXTURE=$<TARGET_FILE:module_import_body_test>
      -DABCD_IMPORT_DEPENDENCY=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/module-import-dep.mjs
      -DABCD_IMPORT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/module_import_oracle.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/module_imports.cmake)
    add_test(NAME created_members_scale_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:created_members_scale_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/created_members_scale.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=created-members-scale.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    add_test(NAME created_members_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:created_members_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/created_members.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=created-members.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    add_test(NAME class_method_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:class_method_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/class_method.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=class-method.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    add_test(NAME class_accessor_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:class_accessor_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/class_accessor.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=class-accessor.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    add_test(NAME created_class_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:created_class_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/created_class.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=created-class.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    add_test(NAME derived_class_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:derived_class_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/derived_class.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=derived-class.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    if(TARGET historical_async_member_sample_test)
      add_test(NAME historical_async_member_js_test COMMAND ${CMAKE_COMMAND}
        -DABCD_ASYNC_FIXTURE=$<TARGET_FILE:historical_async_member_sample_test>
        -DABCD_ASYNC_SAMPLE=${ABCD_PAIRED_CORPUS_ROOT}/pasteboard/modules.abc
        -DABCD_ASYNC_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/historical_async_member.mjs
        -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/historical_async_member.cmake)
    endif()
    add_test(NAME base_constructor_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:base_constructor_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/base_constructor.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=base-constructor.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    add_test(NAME created_function_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:created_function_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/created_function.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=created-function.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
    add_test(NAME module_exports_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:module_exports_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/module_exports.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=module-exports.mjs
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
    add_test(NAME constructor_call_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:constructor_call_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/constructor_call.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=constructor-call.mjs
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
    add_test(NAME object_buffer_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:object_buffer_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/object_buffer.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=object-buffer.mjs
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
  add_executable(constant_object_test tests/unit/analysis/constant_object_test.cc)
  target_link_libraries(constant_object_test PRIVATE abcd_analysis)
  add_test(NAME constant_object_test COMMAND constant_object_test)
  add_executable(iterator_test tests/unit/analysis/iterator_test.cc)
  target_link_libraries(iterator_test PRIVATE abcd_analysis)
  add_test(NAME iterator_test COMMAND iterator_test)
  add_executable(first_for_in_test tests/unit/analysis/first_for_in_test.cc)
  target_include_directories(first_for_in_test PRIVATE tests/support)
  target_link_libraries(first_for_in_test PRIVATE abcd_analysis)
  add_test(NAME first_for_in_test COMMAND first_for_in_test)
  add_executable(iter_result_test tests/unit/analysis/iter_result_test.cc)
  target_include_directories(iter_result_test PRIVATE tests/support)
  target_link_libraries(iter_result_test PRIVATE abcd_source)
  add_test(NAME iter_result_test COMMAND iter_result_test)
  if(ABCD_NODE_EXECUTABLE)
    add_test(NAME iter_result_js_test COMMAND ${CMAKE_COMMAND}
      -DABCD_CONSTANT_FIXTURE=$<TARGET_FILE:iter_result_test>
      -DABCD_CONSTANT_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/iter_result.mjs
      -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
      -DABCD_OUTPUT_NAME=iter-result.mjs
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/linear_constants.cmake)
  endif()
  add_executable(private_property_test tests/unit/analysis/private_property_test.cc)
  target_link_libraries(private_property_test PRIVATE abcd_analysis)
  add_test(NAME private_property_test COMMAND private_property_test)
  if(EXISTS "${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc")
    add_executable(guarded_global_call_sample_test
      tests/integration/guarded_global_call_sample.cc)
    target_include_directories(guarded_global_call_sample_test PRIVATE src/source)
    target_link_libraries(guarded_global_call_sample_test PRIVATE abcd_source)
    add_test(NAME guarded_global_call_sample_test
      COMMAND guarded_global_call_sample_test
      ${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc)
    if(ABCD_NODE_EXECUTABLE)
      add_test(NAME guarded_global_call_js_test COMMAND ${CMAKE_COMMAND}
        -DABCD_FIXTURE=$<TARGET_FILE:guarded_global_call_sample_test>
        -DABCD_SAMPLE=${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc
        -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
        -DABCD_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/guarded_global_call.mjs
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/guarded_global_call.cmake)
    endif()
  endif()
  if(EXISTS "${ABCD_PAIRED_CORPUS_ROOT}/push-kit-sample-code-clientdemo-arkts/widgets.abc")
    add_executable(historical_guarded_global_sample_test
      tests/integration/historical_guarded_global_sample.cc)
    target_include_directories(historical_guarded_global_sample_test PRIVATE src/source)
    target_link_libraries(historical_guarded_global_sample_test PRIVATE abcd_source)
    add_test(NAME historical_guarded_global_sample_test
      COMMAND historical_guarded_global_sample_test
      ${ABCD_PAIRED_CORPUS_ROOT}/push-kit-sample-code-clientdemo-arkts/widgets.abc)
    if(ABCD_NODE_EXECUTABLE)
      add_test(NAME historical_guarded_global_js_test COMMAND ${CMAKE_COMMAND}
        -DABCD_FIXTURE=$<TARGET_FILE:historical_guarded_global_sample_test>
        -DABCD_SAMPLE=${ABCD_PAIRED_CORPUS_ROOT}/push-kit-sample-code-clientdemo-arkts/widgets.abc
        -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
        -DABCD_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/guarded_global_call.mjs
        -DABCD_OUTPUT_NAME=historical-guarded-global-call.mjs
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/guarded_global_call.cmake)
    endif()
  endif()

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
    if(ABCD_NODE_EXECUTABLE AND
       EXISTS "${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc")
      add_test(NAME structure_source_cli_test COMMAND ${CMAKE_COMMAND}
        -DABCD_STRUCTURE=$<TARGET_FILE:abc_structure>
        -DABCD_FIXTURE=$<TARGET_FILE:index_fixture>
        -DABCD_SAMPLE=${ABCD_PAIRED_CORPUS_ROOT}/Immersive/modules.abc
        -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
        -DABCD_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_program_sample.mjs
        -DABCD_STATIC_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/static_fields_sample.mjs
        -DABCD_ROUTES_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_constants_sample.mjs
        -DABCD_BACKUP_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/entry_backup_ability.mjs
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/structure_source_cli.cmake)
    endif()
    if(ABCD_NODE_EXECUTABLE AND
       EXISTS "${ABCD_PAIRED_CORPUS_ROOT}/pasteboard/modules.abc" AND
       EXISTS "${ABCD_PAIRED_CORPUS_ROOT}/MultiCommunityApplication/modules.abc")
      add_test(NAME record_constructor_source_test COMMAND ${CMAKE_COMMAND}
        -DABCD_STRUCTURE=$<TARGET_FILE:abc_structure>
        -DABCD_SAMPLE=${ABCD_PAIRED_CORPUS_ROOT}/pasteboard/modules.abc
        -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
        -DABCD_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_constructor.mjs
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/record_constructor.cmake)
      add_test(NAME historical_record_source_test COMMAND ${CMAKE_COMMAND}
        -DABCD_STRUCTURE=$<TARGET_FILE:abc_structure>
        -DABCD_SAMPLE=${ABCD_PAIRED_CORPUS_ROOT}/pasteboard/modules.abc
        -DABCD_UNSUPPORTED_SAMPLE=${ABCD_PAIRED_CORPUS_ROOT}/MultiCommunityApplication/modules.abc
        -DABCD_NODE=${ABCD_NODE_EXECUTABLE}
        -DABCD_ORACLE=${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/historical_common_constants.mjs
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/historical_record_source.cmake)
    endif()
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
