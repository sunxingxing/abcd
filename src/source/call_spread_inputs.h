#ifndef ABCD_SOURCE_CALL_SPREAD_INPUTS_H_
#define ABCD_SOURCE_CALL_SPREAD_INPUTS_H_
#include "evaluation_plan.h"
#include "tree_checks.h"
namespace abcd::source {
// Admission candidates only. LiteralPlan and the common event-order verifier
// still prove private construction; CallPlan proves the sole consumer/reference.
// Indexing is linear in operations plus disjoint array steps, prepaid once.
inline std::vector<std::optional<analysis::UseId>> IndexCallSpreadInputs(
    const analysis::ValueProgram& graph, const EvaluationPlan& plan, TreeCheckBudget& budget) {
  std::vector<std::optional<analysis::UseId>> result;
  if (!ArrayInitializations(plan)) return result;
  bool needed=false;
  for(const auto& op:graph.ops) {
    budget.Step(); const auto* call=std::get_if<analysis::ValueInvoke>(&op.operation);
    needed |= op.life==analysis::ValueLife::kLive && ((call && call->form==analysis::CallArgumentForm::kApplyArray) ||
        std::holds_alternative<analysis::ValueConstructApply>(op.operation));
  }
  if(!needed) return result;
  budget.Array(result,graph.ops.size());
  for(const auto& op:graph.ops) {
    budget.Step(); const auto* call=std::get_if<analysis::ValueInvoke>(&op.operation);
    if(op.life!=analysis::ValueLife::kLive)continue;
    const bool construct=std::holds_alternative<analysis::ValueConstructApply>(op.operation);
    if(construct ? op.use_count!=2 : (!call || call->form!=analysis::CallArgumentForm::kApplyArray || op.use_count!=3))continue;
    // NEWOBJAPPLY's indexed extraction needs no CallSpread premise. Its
    // private dense array/cursor proof and direct-constructor identity are
    // independently rechecked by evaluation and CallPlan before publication.
    const analysis::UseId use{op.first_use.value+(construct?1u:2u)};
    const auto operand=ResolveEvaluationOperand(plan,graph.uses[use.value].operand);
    const auto* value=std::get_if<analysis::ValueId>(&operand);
    if(!value)continue;
    const auto* producer=std::get_if<analysis::OpResult>(&graph.values[value->value].origin);
    if(producer && std::holds_alternative<analysis::ValueArrayAllocation>(graph.ops[producer->op.value].operation))
      result[producer->op.value]=use;
  }
  return result;
}
}
#endif
