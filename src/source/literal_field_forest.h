#ifndef ABCD_SOURCE_LITERAL_FIELD_FOREST_H_
#define ABCD_SOURCE_LITERAL_FIELD_FOREST_H_
#include "evaluation_plan.h"
#include "tree_checks.h"
namespace abcd::source {
struct LiteralFieldCandidates {
  std::vector<LiteralFieldObject> objects;
  std::vector<std::optional<analysis::OpId>> executions;
};
// A private-construction/call census, NOT relocation permission. In particular
// it supplies the real throwing execution scope for pure definition consumers.
LiteralFieldCandidates IndexLiteralFieldCandidates(
    const analysis::ValueProgram&, const EvaluationPlan&, TreeCheckBudget&);
void BuildLiteralFieldForests(const analysis::ValueProgram&, EvaluationPlan&,
                              TreeCheckBudget&);
void VerifyLiteralFieldForests(const analysis::ValueProgram&,
                               const EvaluationPlan&, TreeCheckBudget&);
inline bool LiteralFieldPrefix(const EvaluationPlan& plan, analysis::OpId id) {
  if (id.value < plan.literal_fields.prefix_fields.size() &&
      plan.literal_fields.prefix_fields[id.value])
    return true;
  const auto peer = GuardedPeer(plan, id);
  return peer && !GuardedInput(plan, id) &&
         peer->value < plan.literal_fields.prefix_fields.size() &&
         plan.literal_fields.prefix_fields[peer->value].has_value();
}
}  // namespace abcd::source
#endif
