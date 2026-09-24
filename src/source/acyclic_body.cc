#include "abcd/source/acyclic_body.h"

#include <algorithm>

#include "branch_staged_calls.h"
#include "branch_source_facts.h"
#include "module_binding_name.h"
#include "ordered_body.h"

namespace abcd::source {
namespace {
AcyclicResult WriteBody(const abc::MethodBody& body, ScalarNames names,
                        std::ostream& output, AcyclicBudget budget,
                        const MethodConstants* constants, bool self_loops,
                        const BranchSourceFacts* facts = nullptr) {
  if (!body.tries.empty() || !body.handlers.empty())
    return BodyEmitFailure{BodyEmitError::kExceptionRegions, {}, {}, {}};
  const auto built = analysis::BuildNormalFlow(body, {budget.bytes});
  if (const auto* error = std::get_if<analysis::FlowFailure>(&built))
    return *error;
  const auto& graph = std::get<analysis::NormalFlow>(built);
  if (graph.blocks.capacity() > budget.bytes / sizeof(analysis::FlowBlock))
    return AcyclicError::kResourceLimit;
  const auto graph_bytes =
      graph.blocks.capacity() * sizeof(analysis::FlowBlock);
  const auto analyzed = self_loops ? analysis::AnalyzeBlockLoopAccumulator(
                                         body, graph, names.registers.size(),
                                         {budget.bytes - graph_bytes},
                                         facts ? std::span<const std::uint8_t>(
                                                     facts->language_loads)
                                               : std::span<const std::uint8_t>{})
                                   : analysis::AnalyzeAcyclicAccumulator(
                                         body, graph, names.registers.size(),
                                         {budget.bytes - graph_bytes},
                                         facts ? std::span<const std::uint8_t>(
                                                     facts->language_loads)
                                               : std::span<const std::uint8_t>{});
  if (const auto* error = std::get_if<analysis::AccumulatorFailure>(&analyzed))
    return *error;
  const auto& flow = std::get<analysis::AccumulatorFlow>(analyzed);
  const auto analysis_budget = budget.bytes - graph_bytes;
  if (flow.allocated_bytes > analysis_budget)
    return AcyclicError::kResourceLimit;
  std::size_t nesting = flow.order.blocks.size() - 1;
  for (std::size_t position = 0; position < flow.order.blocks.size();
       ++position) {
    const auto id = flow.order.blocks[position];
    const auto& block = graph.blocks[id.value];
    for (std::size_t i = 0; i < block.successor_count; ++i)
      if (block.successors[i].target == id)
        nesting = std::max(nesting, flow.order.blocks.size() - position);
  }
  if (nesting > budget.max_nesting) return AcyclicError::kNestingLimit;
  const auto staged = BuildBranchStagedCalls(
      body, graph, names.registers.size(),
      analysis_budget - flow.allocated_bytes, budget.work);
  if (const auto* error = std::get_if<BodyEmitFailure>(&staged)) return *error;
  const auto& calls = std::get<BranchStagedCalls>(staged);
  if (calls.AllocatedBytes() > analysis_budget - flow.allocated_bytes)
    return AcyclicError::kResourceLimit;
  const auto result = WriteOrderedBody(body, graph, flow, names, output,
                                       budget.output, constants,
                                       {analysis_budget - flow.allocated_bytes -
                                        calls.AllocatedBytes()},
                                       facts, calls.calls);
  if (const auto* error = std::get_if<BodyEmitFailure>(&result)) return *error;
  return std::get<std::size_t>(result);
}
}  // namespace
AcyclicResult WriteAcyclicBody(const abc::MethodBody& body, ScalarNames names,
                               std::ostream& output, AcyclicBudget budget,
                               const MethodConstants* constants) {
  return WriteBody(body, names, output, budget, constants, false);
}
AcyclicResult WriteBlockLoopBody(const abc::MethodBody& body, ScalarNames names,
                                 std::ostream& output, AcyclicBudget budget,
                                 const MethodConstants* constants) {
  return WriteBody(body, names, output, budget, constants, true);
}
AcyclicResult WriteCapturedBranchBody(
    const abc::MethodBody& body, ScalarNames names, std::ostream& output,
    AcyclicBudget budget, const MethodConstants* constants,
    const BranchSourceFacts& facts) {
  constexpr std::string_view kImportPrefix = "$import";
  const auto collision = [](std::string_view name) {
    return name.starts_with(kImportPrefix) ||
           name.starts_with(kLocalModulePrefix);
  };
  if (collision(names.accumulator))
    return BodyEmitFailure{BodyEmitError::kExpression, {}, {},
                           EmitError::kInvalidBinding};
  for (const auto name : names.registers)
    if (collision(name))
      return BodyEmitFailure{BodyEmitError::kExpression, {}, {},
                             EmitError::kInvalidBinding};
  if (facts.AllocatedBytes() > budget.bytes)
    return AcyclicError::kResourceLimit;
  budget.bytes -= facts.AllocatedBytes();
  return WriteBody(body, names, output, budget, constants, true, &facts);
}
}  // namespace abcd::source
