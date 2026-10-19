#ifndef ABCD_SOURCE_REFERENCE_ARGUMENTS_H_
#define ABCD_SOURCE_REFERENCE_ARGUMENTS_H_
#include "call_plan.h"
#include "logical_evaluation.h"
#include "tree_checks.h"
namespace abcd::source {
// Original placements seed only an unmet member Reference. Inline eager
// argument ancestry is indexed once; verification rebuilds it after fusion.
struct ReferenceArguments {
  std::vector<std::uint8_t> calls, arguments;
  ReferenceArguments(const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
      TreeCheckBudget& budget) {
    budget.Array(calls, graph.ops.size()); budget.Array(arguments, graph.ops.size());
    for (std::size_t i = 0; i < graph.ops.size(); ++i) {
      budget.Step();
      const auto property = MemberProperty(graph, evaluation, analysis::OpId{static_cast<std::uint32_t>(i)});
      if (!property) continue;
      const auto placement = BeforeLogicalFusion(graph, evaluation, property->value);
      calls[i] = placement.kind == EvaluationKind::kRoot &&
          placement.temporary == TemporaryReason::kCrossRegionValue;
    }
    // Existing eager inline edges run forward in operation order. Propagate
    // the enclosing argument once, without per-selection ancestor walks.
    for (std::size_t i = graph.ops.size(); i-- > 0;) {
      budget.Step();
      const auto placement = BeforeLogicalFusion(graph, evaluation, i);
      if (placement.kind != EvaluationKind::kInline || !placement.consumer) continue;
      arguments[i] = Contains(graph.uses[placement.consumer->value]);
    }
  }
  bool Contains(const analysis::OperandUse& use) const {
    const auto* owner = std::get_if<analysis::OpId>(&use.owner);
    return owner && (arguments[owner->value] ||
        (calls[owner->value] && use.role == analysis::OperandRole::kArgument));
  }
};
}  // namespace abcd::source
#endif
