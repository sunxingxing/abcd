#include "for_in_evaluation.h"
namespace abcd::source {
EvaluationFailure ForInEvaluationFailure(const analysis::ValueProgram& graph, ForInFailure failure) {
  const auto kind = failure.kind == ForInError::kResourceLimit ? EvaluationError::kResourceLimit :
      failure.kind == ForInError::kWorkLimit ? EvaluationError::kWorkLimit : EvaluationError::kUnsupportedOperation;
  EvaluationFailure result{kind}; result.evaluation.for_in = failure;
  if (const auto op = failure.operation ? failure.operation : failure.acquisition)
    result.evaluation.location = OperationLocation(graph, *op);
  result.evaluation.use = failure.use; result.evaluation.value = failure.value;
  return result;
}
std::optional<EvaluationFailure> BuildForInEvaluation(const analysis::ValueProgram& graph,
    EvaluationPlan& plan, analysis::SemanticBudget& left) {
  auto built = BuildForInPlan(graph, left);
  if (const auto* error = std::get_if<ForInFailure>(&built)) return ForInEvaluationFailure(graph, *error);
  plan.for_in = std::get<ForInPlan>(std::move(built));
  left.bytes -= plan.for_in->allocated_bytes; left.work -= plan.for_in->work;
  auto events = BuildForInEvents(graph, *plan.for_in, left);
  if (const auto* error = std::get_if<ForInFailure>(&events)) return ForInEvaluationFailure(graph, *error);
  plan.for_in_events = std::get<ForInEvents>(std::move(events));
  left.bytes -= plan.for_in_events->allocated_bytes; left.work -= plan.for_in_events->work;
  return {};
}
std::variant<std::uint64_t, EvaluationFailure> VerifyForInEvaluation(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan, analysis::SemanticBudget left) {
  bool needed = false;
  for (const auto& op : graph.ops) {
    if (!left.work) return EvaluationError::kWorkLimit;
    --left.work;
    needed |= plan.flow == EvaluationFlow::kPhased && op.life == analysis::ValueLife::kLive && std::holds_alternative<analysis::ValueEnumeration>(op.operation);
  }
  if (needed != plan.for_in.has_value() || needed != plan.for_in_events.has_value()) return EvaluationError::kOwnership;
  if (!needed) return std::uint64_t{graph.ops.size()};
  auto checked = VerifyForInEvents(graph, *plan.for_in, *plan.for_in_events, left);
  if (const auto* error = std::get_if<ForInFailure>(&checked)) return ForInEvaluationFailure(graph, *error);
  return graph.ops.size() + std::get<std::uint64_t>(checked);
}
} // namespace abcd::source
