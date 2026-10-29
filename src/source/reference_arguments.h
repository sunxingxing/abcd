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
    Propagate(graph, evaluation, budget);
    // An object literal argument owns its field definitions, which the
    // literal plan places as initializers rather than inline consumers.
    // Mark them, then propagate once more into their field values.
    if (!evaluation.literals) return;
    bool marked = false;
    for (const auto& object : evaluation.literals->objects) {
      budget.Step();
      // The allocation may be a snapshot crossing the selection; its actual
      // value uses decide whether it is an argument of an unmet Reference.
      const auto& result = graph.ops[object.allocation.value].result;
      if (!result) continue;
      bool argument = false;
      for (auto use = graph.values[result->value].first_use; use && !argument; use = graph.uses[use->value].next) {
        budget.Step();
        argument = graph.uses[use->value].life == analysis::ValueLife::kLive && Contains(graph.uses[use->value]);
      }
      if (!argument) continue;
      for (std::uint32_t d = 0; d < object.count; ++d) {
        budget.Step();
        const auto definition = evaluation.literals->definitions[object.first + d].value;
        if (!arguments[definition]) { arguments[definition] = 1; marked = true; }
      }
    }
    if (marked) Propagate(graph, evaluation, budget);
  }
  void Propagate(const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
      TreeCheckBudget& budget) {
    for (std::size_t i = graph.ops.size(); i-- > 0;) {
      budget.Step();
      const auto placement = BeforeLogicalFusion(graph, evaluation, i);
      if (placement.kind != EvaluationKind::kInline || !placement.consumer) continue;
      arguments[i] = arguments[i] || Contains(graph.uses[placement.consumer->value]);
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
