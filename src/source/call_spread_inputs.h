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
  bool needed=false;
  for(const auto& op:graph.ops) {
    budget.Step(); const auto* call=std::get_if<analysis::ValueInvoke>(&op.operation);
    needed |= op.life==analysis::ValueLife::kLive && call && call->form==analysis::CallArgumentForm::kApplyArray;
  }
  if(!needed) return result;
  budget.Array(result,graph.ops.size());
  for(const auto& op:graph.ops) {
    budget.Step(); const auto* call=std::get_if<analysis::ValueInvoke>(&op.operation);
    if(op.life!=analysis::ValueLife::kLive || !call || call->form!=analysis::CallArgumentForm::kApplyArray || op.use_count!=3)continue;
    const analysis::UseId use{op.first_use.value+2};
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
