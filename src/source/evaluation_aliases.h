#ifndef ABCD_SOURCE_EVALUATION_ALIASES_H_
#define ABCD_SOURCE_EVALUATION_ALIASES_H_

#include "evaluation_plan.h"

namespace abcd::source {
struct EvaluationAliasCost {
  std::size_t bytes;
  std::uint64_t work;
};
// A proved SSA identity is projected only onto an existing language binding.
// Catch bindings/staging and mutable unresolved phi locations stay distinct.
bool IsIdentityPhiInput(const EvaluationPlan& plan, const analysis::OperandUse& use);
bool SourceIdentityRoot(const analysis::ValueProgram& graph, analysis::ValueId root);
std::optional<analysis::ValueId> SourcePhiAlias(const analysis::ValueProgram& graph,
    std::span<const analysis::ValueId> identities, analysis::ValueId value);
// Internal, after fresh availability/use checking and liveness projection.
// Changes only the local plan under construction; discard it on failure.
[[nodiscard]] std::variant<EvaluationAliasCost, EvaluationFailure> BuildEvaluationAliases(
    const analysis::ValueProgram& graph, EvaluationPlan& plan, analysis::SemanticBudget budget);
// Checks local copy equations/flat roots and independently recomputes anchored
// phi/copy SCC identities against the original graph. No layout certificates.
[[nodiscard]] std::variant<std::uint64_t, EvaluationFailure> VerifyEvaluationAliases(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan, analysis::SemanticBudget budget);
}  // namespace abcd::source
#endif
