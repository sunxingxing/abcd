#include "exception_inspect.h"

#include <cstdlib>
#include <type_traits>
#include <nlohmann/json.hpp>
#include "abcd/analysis/exception_flow.h"
#include "abcd/analysis/exception_ssa.h"
#include "abcd/analysis/exception_value_flow.h"
#include "abcd/analysis/exception_environment.h"
#include "abcd/analysis/exception_inputs.h"
#include "abcd/analysis/exception_regions.h"
#include "abcd/analysis/semantic_region.h"

int InspectExceptionFlow(const abcd::abc::MethodBody& body,
    abcd::abc::ProfileId profile, std::uint64_t frame_size,
    std::uint32_t first_abi_register, std::ostream& output) {
  using namespace abcd;
  const analysis::ExceptionFlowBudget budget;
  const auto built = analysis::BuildExceptionFlow(body, profile, frame_size, budget);
  const auto* flow = std::get_if<analysis::ExceptionFlow>(&built);
  if (!flow) {
    if (const auto* failure = std::get_if<analysis::SemanticFailure>(&built))
      output << nlohmann::json{{"semantic_error", static_cast<unsigned>(failure->kind)},
                               {"pc", failure->pc.value}} << '\n';
    else if (const auto* failure = std::get_if<analysis::FlowFailure>(&built))
      output << nlohmann::json{{"flow_error", static_cast<unsigned>(failure->kind)},
                               {"pc", failure->offset.value}} << '\n';
    else {
      const auto& dispatch = std::get<analysis::DispatchFailure>(built);
      output << nlohmann::json{{"dispatch_error", static_cast<unsigned>(dispatch.kind)},
                               {"pc", dispatch.offset.value}} << '\n';
    }
    return EXIT_FAILURE;
  }
  analysis::IdfQuota quota{analysis::kDefaultSemanticWork};
  const auto renamed = analysis::ExceptionSsa::Build(*flow, frame_size, quota,
      {budget.bytes - flow->allocated_bytes});
  const auto* ssa = std::get_if<analysis::ExceptionSsa>(&renamed);
  if (!ssa) {
    std::visit([&](const auto& error) {
      using T = std::decay_t<decltype(error)>;
      if constexpr (std::is_same_v<T, analysis::RegisterUseFailure>)
        output << nlohmann::json{{"register_use_error", static_cast<unsigned>(error.kind)},
                                 {"pc", error.offset.value}} << '\n';
      else if constexpr (!std::is_same_v<T, analysis::ExceptionSsa>)
        output << nlohmann::json{{"ssa_error_domain", renamed.index()},
                                 {"ssa_error", static_cast<unsigned>(error)}} << '\n';
    }, renamed);
    return EXIT_FAILURE;
  }
  auto remaining = budget.bytes - flow->allocated_bytes - ssa->AllocatedBytes();
  std::vector<analysis::EntryValueFact> entries;
  if (ssa->Values().size() > remaining / sizeof(analysis::EntryValueFact)) return EXIT_FAILURE;
  entries.reserve(ssa->Values().size());
  if (entries.capacity() > remaining / sizeof(analysis::EntryValueFact)) return EXIT_FAILURE;
  remaining -= entries.capacity() * sizeof(analysis::EntryValueFact);
  for (std::size_t i = 0; i < ssa->Values().size(); ++i) {
    const auto& value = ssa->Values()[i];
    const auto* reg = std::get_if<abc::RegisterId>(&value.slot);
    if (reg && reg->value >= first_abi_register &&
        std::holds_alternative<analysis::MethodEntry>(value.origin))
      entries.push_back({analysis::SsaValueId{static_cast<std::uint32_t>(i)},
                         analysis::ValueKinds::Language()});
  }
  const auto analyzed = analysis::ExceptionValueFlow::Build(*flow, *ssa, entries, quota, {remaining});
  const auto* values = std::get_if<analysis::ExceptionValueFlow>(&analyzed);
  if (!values) {
    const auto& failure = std::get<analysis::ValueFlowFailure>(analyzed);
    output << nlohmann::json{{"value_flow_error", static_cast<unsigned>(failure.kind)}} << '\n';
    return EXIT_FAILURE;
  }
  remaining -= values->AllocatedBytes();
  const auto bound = analysis::AnalyzeExceptionEnvironments(*flow, {remaining});
  const auto* environments = std::get_if<analysis::EnvironmentFlow>(&bound);
  if (!environments) {
    const auto& failure = std::get<analysis::EnvironmentFlowFailure>(bound);
    output << nlohmann::json{{"environment_error", static_cast<unsigned>(failure.kind)},
                             {"pc", failure.offset.value}} << '\n';
    return EXIT_FAILURE;
  }
  const auto environment_ref = [](analysis::EnvironmentRef ref) {
    if (const auto* local = std::get_if<analysis::EnvironmentId>(&ref))
      return nlohmann::json{{"environment", local->value}};
    return nlohmann::json{{"captured_depth", std::get<analysis::CapturedDepth>(ref).value}};
  };
  const auto checked = analysis::VerifyExceptionInputs(*flow, *ssa, *environments, *values,
      analysis::kDefaultSemanticWork - quota.Consumed());
  const auto* inputs = std::get_if<analysis::ExceptionInputProof>(&checked);
  if (environments->allocated_bytes > remaining) return EXIT_FAILURE;
  remaining -= environments->allocated_bytes;
  const auto recovered = analysis::BuildExceptionRegions(body, *flow,
      {remaining, analysis::kDefaultSemanticWork - quota.Consumed()});
  const auto* regions = std::get_if<analysis::ExceptionRegions>(&recovered);
  const auto build_tree = [&]() -> analysis::RegionResult {
    if (!regions) return analysis::RegionFailure{analysis::RegionError::kExceptionShape, {}};
    if (regions->allocated_bytes > remaining)
      return analysis::RegionFailure{analysis::RegionError::kResourceLimit, {}};
    const auto available = remaining - regions->allocated_bytes;
    const auto built_normal = analysis::BuildNormalFlow(body, {available});
    const auto* normal = std::get_if<analysis::NormalFlow>(&built_normal);
    if (!normal) {
      const auto& failure = std::get<analysis::FlowFailure>(built_normal);
      return analysis::RegionFailure{
          failure.kind == analysis::FlowError::kResourceLimit
              ? analysis::RegionError::kResourceLimit : analysis::RegionError::kExceptionShape,
          failure.offset};
    }
    const auto normal_bytes = normal->blocks.capacity() * sizeof(analysis::FlowBlock);
    if (normal_bytes > available)
      return analysis::RegionFailure{analysis::RegionError::kResourceLimit, {}};
    return analysis::BuildExceptionRegionTree(*flow, *regions, *normal, available - normal_bytes);
  };
  const auto tree_result = build_tree();
  const auto* tree = std::get_if<analysis::SemanticRegionTree>(&tree_result);
  output << nlohmann::json{{"schema", 7}, {"instructions", flow->instructions.size()},
      {"nodes", flow->nodes.size()}, {"source_complete", false}, {"ssa_complete", false},
      {"register_ssa_complete", true}, {"value_flow_complete", true},
      {"environment_topology_complete", true}, {"lexical_values_proven", false},
      {"source_inputs_proven", inputs != nullptr},
      {"exception_regions_proven", regions != nullptr},
      {"exception_tree_proven", tree != nullptr},
      {"requires_language_abi", true}, {"pending_exception_values_proven", false},
      {"ssa_values", ssa->Values().size()}}
      << '\n';
  if (tree) {
    output << nlohmann::json{{"tree_catches", tree->catches},
                             {"tree_blocks", tree->order_positions.size()}} << '\n';
  } else {
    const auto& failure = std::get<analysis::RegionFailure>(tree_result);
    output << nlohmann::json{{"region_tree_error", static_cast<unsigned>(failure.kind)},
                             {"pc", failure.pc.value}} << '\n';
  }
  if (!regions) {
    const auto& failure = std::get<analysis::CatchRegionFailure>(recovered);
    output << nlohmann::json{{"catch_region_error", static_cast<unsigned>(failure.kind)},
                             {"pc", failure.pc.value}} << '\n';
  } else {
    for (std::size_t i = 0; i < regions->regions.size(); ++i) {
      const auto& region = regions->regions[i];
      nlohmann::json row{{"try_region", i}, {"handler", region.handler_id.value},
          {"protected_instructions", {region.body.begin.value, region.body.end.value}},
          {"handler_instructions", {region.handler.begin.value, region.handler.end.value}}};
      if (region.parent) {
        row["parent_region"] = region.parent->region.value;
        row["parent_part"] = static_cast<unsigned>(region.parent->part);
      }
      if (region.outer_dispatch) row["outer_dispatch"] = region.outer_dispatch->value;
      output << row << '\n';
    }
  }
  if (inputs) {
    output << nlohmann::json{{"verified_reads", inputs->reads}} << '\n';
  } else {
    const auto& failure = std::get<analysis::ExceptionInputFailure>(checked);
    nlohmann::json row{{"input_error", static_cast<unsigned>(failure.kind)},
                       {"pc", failure.pc.value}};
    if (failure.node) row["node"] = failure.node->value;
    if (failure.value) row["ssa_value"] = failure.value->value;
    output << row << '\n';
  }
  for (std::size_t i = 0; i < flow->nodes.size(); ++i) {
    const auto& node = flow->nodes[i];
    const auto& instruction = flow->instructions[node.instruction.value];
    nlohmann::json row{{"node", i}, {"instruction", node.instruction.value},
        {"pc", instruction.pc.value}, {"phase", static_cast<unsigned>(node.phase)},
        {"successors", nlohmann::json::array()}};
    const analysis::ExceptionNodeId id{static_cast<std::uint32_t>(i)};
    if (environments->before[i])
      row["environment_before"] = environment_ref(*environments->before[i]);
    row["reads"] = nlohmann::json::array();
    for (const auto& read : ssa->Reads(id))
      row["reads"].push_back({{"ordinal", read.ordinal}, {"value", read.value.value}});
    if (const auto write = ssa->Write(id)) row["write_value"] = write->value;
    for (std::size_t e = 0; e < node.successor_count; ++e)
      row["successors"].push_back({{"target", node.successors[e].target.value},
          {"kind", static_cast<unsigned>(node.successors[e].kind)}});
    if (node.phase == analysis::ExceptionPhase::kCommit &&
        instruction.effects.normal_registers.write) {
      const auto& slot = *instruction.effects.normal_registers.write;
      if (const auto* reg = std::get_if<abc::RegisterId>(&slot))
        row["register_write"] = reg->value;
      else row["accumulator_write"] = true;
    }
    if (node.phase == analysis::ExceptionPhase::kCatch)
      row["catch_accumulator"] = true;
    if (node.exception)
      row["exceptional_completion"] = static_cast<unsigned>(node.exception->completion);
    if (node.exception && !node.exception->target)
      row["propagates_to_caller"] = true;
    output << row << '\n';
  }
  for (const auto& allocation : environments->allocations) {
    const auto* definition = environments->environments.Find(allocation.environment);
    output << nlohmann::json{{"allocation_node", allocation.instruction},
        {"environment", allocation.environment.value},
        {"parent", environment_ref(definition->parent)},
        {"slots", definition->allocation.slots}} << '\n';
  }
  for (const auto& access : environments->bindings)
    output << nlohmann::json{{"binding_node", access.instruction},
        {"owner", environment_ref(access.binding.environment)},
        {"slot", access.binding.slot.value}} << '\n';
  for (std::size_t i = 0; i < ssa->Values().size(); ++i) {
    const auto& value = ssa->Values()[i];
    nlohmann::json row{{"ssa_value", i}};
    const auto kinds = values->Facts()[i];
    row["value_kinds"] = nlohmann::json::array();
    for (const auto kind : {analysis::VmValueKind::kLanguage, analysis::VmValueKind::kHole,
                           analysis::VmValueKind::kEnvironment, analysis::VmValueKind::kOtherInternal})
      if (kinds.Contains(kind)) row["value_kinds"].push_back(static_cast<unsigned>(kind));
    if (kinds.IsNonNaNNumber()) row["non_nan_number"] = true;
    if (kinds.IsString()) row["string"] = true;
    if (const auto* reg = std::get_if<abc::RegisterId>(&value.slot)) row["register"] = reg->value;
    else row["accumulator"] = true;
    if (const auto* site = std::get_if<analysis::InstructionValue>(&value.origin))
      row["site_node"] = site->instruction.value;
    else if (const auto* phi = std::get_if<analysis::PhiValue>(&value.origin)) {
      row["phi_node"] = phi->block.value;
      row["inputs"] = nlohmann::json::array();
      for (const auto& input : ssa->Inputs({static_cast<std::uint32_t>(i)})) {
        nlohmann::json item{{"value", input.value.value}};
        if (const auto* edge = std::get_if<analysis::SsaEdge>(&input.edge)) {
          item["source_node"] = edge->source.value;
          item["successor"] = edge->successor;
        } else item["entry"] = true;
        row["inputs"].push_back(std::move(item));
      }
    } else row["entry"] = true;
    output << row << '\n';
  }
  return output ? EXIT_SUCCESS : EXIT_FAILURE;
}
