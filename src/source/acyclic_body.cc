#include "abcd/source/acyclic_body.h"

#include "ordered_body.h"

namespace abcd::source {
AcyclicResult WriteAcyclicBody(const abc::MethodBody& body, ScalarNames names,
                               std::ostream& output, AcyclicBudget budget,
                               const MethodConstants* constants) {
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
  const auto analyzed = analysis::AnalyzeAcyclicAccumulator(
      body, graph, names.registers.size(), {budget.bytes - graph_bytes});
  if (const auto* error = std::get_if<analysis::AccumulatorFailure>(&analyzed))
    return *error;
  const auto& flow = std::get<analysis::AccumulatorFlow>(analyzed);
  if (flow.order.blocks.size() - 1 > budget.max_nesting)
    return AcyclicError::kNestingLimit;
  const auto result = WriteOrderedBody(body, graph, flow, names, output,
                                       budget.output, constants);
  if (const auto* error = std::get_if<BodyEmitFailure>(&result)) return *error;
  return std::get<std::size_t>(result);
}
}  // namespace abcd::source
