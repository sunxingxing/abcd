#ifndef ABCD_TEST_SOURCE_INVOCATION_VALUES_H_
#define ABCD_TEST_SOURCE_INVOCATION_VALUES_H_
#include "../../../src/source/recovery_flow.h"
#include <cstdlib>
#include <iostream>
#include <source_location>
namespace abcd::test {
inline void InvocationCheck(bool good,std::source_location at=std::source_location::current()) {
  if(!good){std::cerr<<"invocation value check "<<at.line()<<'\n';std::abort();}
}
inline void CheckInvocationValues(const source::FunctionTree& tree,const source::OriginalFlow& original) {
  using namespace source;using namespace analysis;
  std::vector<std::optional<SymbolId>> aliases(tree.symbols.size());
  std::vector<std::optional<PrimitiveId>> literals(tree.symbols.size());
  std::vector<std::optional<ExprId>> replacements(tree.expressions.size());
  std::vector<std::optional<ExpressionTransport>> transports(tree.expressions.size());
  std::vector<std::uint8_t> omitted(tree.statements.size()),normal(tree.expressions.size());
  std::vector<ExpressionEvaluation> effects(tree.expressions.size(),ExpressionEvaluation::kRequired);
  LegacyFlowProjection projection{aliases,literals,replacements,omitted,effects,normal};projection.original=&original;projection.transports=transports;
  const auto current=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  InvocationCheck(current.invocation_values.size()==1);
  const auto& entry=current.invocation_values.front();
  InvocationCheck(entry.parameter.value<tree.parameters.size()&&tree.parameters[entry.parameter.value].rest);
  const auto binding=tree.parameters[entry.parameter.value].symbol;
  const auto& definition=current.definitions[entry.definition.value];
  InvocationCheck(definition.kind==FlowDefinitionKind::kRestParameter&&definition.binding==binding);
  const auto& allocation=current.events[entry.creation.value];
  InvocationCheck(!allocation.expression&&allocation.entry_evaluation&&allocation.effects.has_value());
  InvocationCheck(HasLanguageEffect(allocation.effects->normal,LanguageEffect::kAllocation));
  InvocationCheck(!HasLanguageEffect(allocation.effects->normal,LanguageEffect::kUserCode));
  bool success=false;
  for(auto edge=allocation.outgoing;edge;edge=current.edges[*edge].outgoing) {
    const auto& e=current.edges[*edge];
    if(e.completion==FlowCompletion::kNormal){InvocationCheck(e.to==definition.point);success=true;}
    else InvocationCheck(e.completion==FlowCompletion::kThrow&&e.to==current.terminal);
  }
  InvocationCheck(success);
  std::vector<OpId> owned;
  // Inventory the independently checked rest value, not the newly added
  // invocation marker. Missing whole correspondence categories must fail.
  for(unsigned i=0;i<original.operations.size();++i) {
    const auto& operation=original.operations[i];if(!operation.result_expression)continue;
    const auto* read=std::get_if<SymbolExpr>(&tree.expressions[operation.result_expression->value].expression);
    if(!read||read->symbol!=binding)continue;
    owned.push_back({i});InvocationCheck(operation.invocation_parameter==entry.parameter);
    InvocationCheck(operation.completion==OriginalCompletionBoundary::kInvocationBinding);
    InvocationCheck(current.original_invocations[i]==entry.parameter);
    InvocationCheck(!current.original_actions[i]&&!current.original_effects[operation.result_expression->value]);
    InvocationCheck(FindFlowOriginalResult(current,OpId{i}).has_value());
    for(unsigned phase=0;phase<original.phases.size();++phase)if(original.phases[phase].operation==OpId{i}) {
      const auto& fact=current.original_phases[phase];InvocationCheck(fact.has_value());
      const auto* invocation=std::get_if<FlowInvocationPhase>(&*fact);
      InvocationCheck(invocation&&invocation->parameter==entry.parameter);
      InvocationCheck(!FindOriginalPhase(current,WorkBlockId{phase},original.phases[phase].phase,OpId{i}));
    }
    transports[operation.result_expression->value]=ExpressionTransport{*operation.result_expression,PrimitiveId{0}};
  }
  InvocationCheck(owned.size()==1);
  auto candidate=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  InvocationCheck(candidate.invocation_values.size()==1); // Read elision cannot delete native signature creation.
  for(const auto id:owned)InvocationCheck(!candidate.original_actions[id.value]);
  auto missing=original;for(const auto id:owned)missing.operations[id.value].invocation_parameter.reset();
  for(auto& transport:transports)transport.reset();projection.original=&missing;
  candidate=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{},std::move(candidate)));
  for(const auto id:owned)InvocationCheck(!candidate.original_invocations[id.value]&&!FindFlowOriginalResult(candidate,id));
  if(!original.phases.empty()) {
    auto wrong=original;
    for(const auto id:owned) {
      const auto& operation=wrong.operations[id.value];InvocationCheck(operation.result.has_value());
      std::get<OpResult>(wrong.values[operation.result->value].origin).commit=operation.block;
    }
    projection.original=&wrong;
    candidate=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{},std::move(candidate)));
    for(const auto id:owned)InvocationCheck(!FindFlowOriginalResult(candidate,id));
  }
  projection.original=&original;
  candidate=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{},std::move(candidate)));
  for(const auto id:owned)InvocationCheck(candidate.original_invocations[id.value]==entry.parameter);
  projection.original=nullptr;
  candidate=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{},std::move(candidate)));
  InvocationCheck(candidate.original_invocations.empty()&&candidate.invocation_values.size()==1);
  projection.original=&original;
  InvocationCheck(std::holds_alternative<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{current.allocated_bytes,current.work})));
  InvocationCheck(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{current.allocated_bytes-1,current.work}))==TreeStructureError::kResourceLimit);
  InvocationCheck(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{current.allocated_bytes,current.work-1}))==TreeStructureError::kWorkLimit);
}
}
#endif
