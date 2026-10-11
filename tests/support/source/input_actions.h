#ifndef ABCD_TEST_SOURCE_INPUT_ACTIONS_H_
#define ABCD_TEST_SOURCE_INPUT_ACTIONS_H_
#include "../../../src/source/recovery_flow.h"
#include <cstdlib>
#include <iostream>
#include <source_location>
namespace abcd::test {
inline void InputActionCheck(bool good,std::source_location at=std::source_location::current()) {
  if(!good){std::cerr<<"input action check "<<at.line()<<'\n';std::abort();}
}
struct InputActionCost {std::uint64_t work;std::size_t bytes;};
inline InputActionCost CheckInputActions(const source::FunctionTree& tree,const source::OriginalFlow& original) {
  using namespace source;using namespace analysis;
  std::vector<std::optional<SymbolId>> aliases(tree.symbols.size());
  std::vector<std::optional<PrimitiveId>> literals(tree.symbols.size());
  std::vector<std::optional<ExprId>> replacements(tree.expressions.size());
  std::vector<std::optional<ExpressionTransport>> transports(tree.expressions.size());
  std::vector<std::uint8_t> omitted(tree.statements.size()),normal(tree.expressions.size());
  std::vector<ExpressionEvaluation> effects(tree.expressions.size(),ExpressionEvaluation::kRequired);
  LegacyFlowProjection projection{aliases,literals,replacements,omitted,effects,normal};projection.original=&original;projection.transports=transports;
  const auto current=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  std::vector<std::uint8_t> checked(original.operations.size());
  // Inventory checked value correspondences, not the new native-action table:
  // deleting a whole action category must not make this test pass vacuously.
  for(unsigned i=0;i<original.operations.size();++i) {
    const auto& operation=original.operations[i];if(!operation.result_expression)continue;
    const auto expression=*operation.result_expression;const auto& node=tree.expressions[expression.value].expression;
    const auto* read=std::get_if<SymbolExpr>(&node);
    const bool self=read&&tree.self==read->symbol;
    if(!self&&!std::holds_alternative<ThisExpr>(node)&&!std::holds_alternative<RealmObjectExpr>(node)&&
        !std::holds_alternative<SymbolConstructorExpr>(node)&&!std::holds_alternative<ArgumentSnapshotExpr>(node)&&
        !std::holds_alternative<GlobalNameExpr>(node))continue;
    checked[i]=1;InputActionCheck(operation.intrinsic_expression==expression);
    const auto& action=current.original_actions[i];InputActionCheck(action.has_value());
    InputActionCheck(current.events[action->before.value].kind==(self?FlowEventKind::kRead:FlowEventKind::kEvaluation));
    InputActionCheck(action->normal==current.normal_exits[expression.value]);
    InputActionCheck(FindFlowOriginalResult(current,OpId{i}).has_value());
    InputActionCheck(current.original_effects[expression.value].has_value());
    if(!original.phases.empty()) {
      InputActionCheck(FindOriginalPhase(current,operation.block,ExceptionPhase::kBefore,OpId{i})==action->before);
      const auto result=FindOriginalResult(original,OpId{i});InputActionCheck(result&&result->commit);
      InputActionCheck(FindOriginalPhase(current,*result->commit,ExceptionPhase::kCommit,OpId{i})==action->normal);
    }
    transports[expression.value]=ExpressionTransport{expression,PrimitiveId{0}};
  }
  auto candidate=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  for(unsigned i=0;i<checked.size();++i)if(checked[i]) {
    const auto expression=*original.operations[i].result_expression;
    InputActionCheck(!candidate.original_actions[i]&&!candidate.original_effects[expression.value]);
    InputActionCheck(!FindFlowOriginalResult(candidate,OpId{i}));
  }
  for(unsigned i=0;i<original.phases.size();++i)
    if(const auto op=original.phases[i].operation;op&&checked[op->value])InputActionCheck(!candidate.original_phases[i]);
  for(unsigned i=0;i<candidate.original_exceptions.size();++i)
    if(checked[original.exceptional_exits[i].op.value]&&candidate.original_exceptions[i])
      InputActionCheck(std::holds_alternative<FlowExcludedResourceFailure>(*candidate.original_exceptions[i]));
  for(auto& transport:transports)transport.reset();
  candidate=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{},std::move(candidate)));
  for(unsigned i=0;i<checked.size();++i)if(checked[i]) {
    InputActionCheck(candidate.original_actions[i].has_value());
    InputActionCheck(FindFlowOriginalResult(candidate,OpId{i}).has_value());
  }
  InputActionCheck(std::holds_alternative<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{current.allocated_bytes,current.work})));
  InputActionCheck(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{current.allocated_bytes-1,current.work}))==TreeStructureError::kResourceLimit);
  InputActionCheck(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{current.allocated_bytes,current.work-1}))==TreeStructureError::kWorkLimit);
  return {current.work,current.allocated_bytes};
}
}
#endif
