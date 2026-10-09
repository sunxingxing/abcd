#include "flow_statements.h"
#include "tree_checks.h"
#include <stdexcept>
namespace abcd::source {
std::variant<FlowStatements,TreeStructureError> AnalyzeFlowStatements(
    const RecoveryFlow& flow,analysis::SemanticBudget allowance) {
  try {
    TreeCheckBudget budget(allowance);FlowStatements result;
    const auto size=flow.statement_semantics.size();
    RequireTree(size==flow.statements.size(),TreeStructureError::kOwnership);
    budget.Array(result.observations,size);
    std::vector<std::uint8_t> seen;budget.Array(seen,size);
    // Validate and seed in semantic traversal order; numeric IDs/arena order
    // are irrelevant. Parent publication must precede its owned children.
    for(const auto id:flow.statement_order) {
      budget.Step();RequireTree(id.value<size&&!seen[id.value]&&flow.statement_semantics[id.value],TreeStructureError::kOwnership);
      const auto& statement=*flow.statement_semantics[id.value];
      if(statement.parent)RequireTree(statement.parent->value<size&&seen[statement.parent->value],TreeStructureError::kOwnership);
      seen[id.value]=1;auto& observation=result.observations[id.value];
      switch(statement.completion) {
        case FlowStatementCompletion::kNormal:break;
        case FlowStatementCompletion::kAbrupt:observation.abrupt=true;break;
        case FlowStatementCompletion::kCleanup:observation.cleanup=true;break;
      }
      switch(statement.progress) {
        case FlowStatementProgress::kFinite:break;
        case FlowStatementProgress::kMayRepeat:observation.repetition=true;break;
      }
    }
    for(std::size_t i=0;i<size;++i) {budget.Step();RequireTree(bool(flow.statement_semantics[i])==bool(seen[i]),TreeStructureError::kOwnership);}
    for(const auto& event:flow.events) {
      budget.Step();if(!event.owner)continue;
      RequireTree(event.owner->value<size&&seen[event.owner->value],TreeStructureError::kOwnership);
      auto& observation=result.observations[event.owner->value];
      if(event.effect==ExpressionEvaluation::kRequired)observation.evaluation=ExpressionEvaluation::kRequired;
      observation.definitions|=event.definition.has_value();
      observation.suspension|=event.kind==FlowEventKind::kSuspend;
    }
    for(auto at=flow.statement_order.rbegin();at!=flow.statement_order.rend();++at) {
      budget.Step();const auto& statement=*flow.statement_semantics[at->value];if(!statement.parent)continue;
      const auto& child=result.observations[at->value];auto& parent=result.observations[statement.parent->value];
      if(child.evaluation==ExpressionEvaluation::kRequired)parent.evaluation=ExpressionEvaluation::kRequired;
      parent.definitions|=child.definitions;parent.abrupt|=child.abrupt;parent.cleanup|=child.cleanup;
      parent.repetition|=child.repetition;parent.suspension|=child.suspension;
    }
    result.allocated_bytes=budget.Bytes();result.work=budget.Work();return result;
  } catch(TreeStructureError error) {return error;
  } catch(const std::bad_alloc&) {return TreeStructureError::kResourceLimit;
  } catch(const std::length_error&) {return TreeStructureError::kResourceLimit;}
}
}
