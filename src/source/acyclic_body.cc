#include "abcd/source/acyclic_body.h"

#include <algorithm>

#include "ordered_body.h"

namespace abcd::source {
namespace {
AcyclicResult WriteBody(const abc::MethodBody& body, ScalarNames names,
                        std::ostream& output, AcyclicBudget budget,
                        const MethodConstants* constants, bool self_loops) {
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
                                         {budget.bytes - graph_bytes})
                                   : analysis::AnalyzeAcyclicAccumulator(
                                         body, graph, names.registers.size(),
                                         {budget.bytes - graph_bytes});
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
  const auto result = WriteOrderedBody(body, graph, flow, names, output,
                                       budget.output, constants,
                                       {analysis_budget - flow.allocated_bytes});
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
}  // namespace abcd::source
