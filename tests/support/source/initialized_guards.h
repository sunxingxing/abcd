#ifndef ABCD_TEST_SOURCE_INITIALIZED_GUARDS_H_
#define ABCD_TEST_SOURCE_INITIALIZED_GUARDS_H_
#include "../../../src/source/recovery_flow.h"
#include <cstdlib>
#include <iostream>
#include <source_location>
namespace abcd::test {
inline void GuardCheck(bool good,std::source_location at=std::source_location::current()) {
  if(!good){std::cerr<<"initialized guard check "<<at.line()<<'\n';std::abort();}
}
struct GuardCost {std::uint64_t work;std::size_t bytes,nodes,edges;};
inline GuardCost CheckInitializedGuards(const source::FunctionTree& tree,const source::OriginalFlow& original,std::size_t count) {
  using namespace source;using namespace analysis;
  std::vector<std::optional<SymbolId>> aliases(tree.symbols.size());
  std::vector<std::optional<PrimitiveId>> literals(tree.symbols.size());
  std::vector<std::optional<ExprId>> replacements(tree.expressions.size());
  std::vector<std::optional<ExpressionTransport>> transports(tree.expressions.size());
  std::vector<std::optional<SymbolId>> read_sources(tree.expressions.size());
  std::vector<std::uint8_t> omitted(tree.statements.size()),normal(tree.expressions.size());
  std::vector<ExpressionEvaluation> effects(tree.expressions.size(),ExpressionEvaluation::kRequired);
  LegacyFlowProjection projection{aliases,literals,replacements,omitted,effects,normal};projection.original=&original;projection.transports=transports;projection.read_sources=read_sources;
  const auto current=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  std::size_t found=0,phases=0,routes=0;
  for(unsigned i=0;i<original.operations.size();++i)if(const auto& witness=original.operations[i].initialized_guard) {
    ++found;const auto value=FindFlowOriginalGuard(current,{i});GuardCheck(value.has_value());
    GuardCheck(!current.original_actions[i]&&!FindFlowOriginalResult(current,{i}));
    GuardCheck(original.operations[witness->load.value].result==witness->value);
    transports[value->expression.value]=ExpressionTransport{value->expression,PrimitiveId{0}};
  }
  GuardCheck(found==count&&found>0);
  for(unsigned i=0;i<original.phases.size();++i) {
    const auto& phase=original.phases[i];if(!phase.operation||!original.operations[phase.operation->value].initialized_guard)continue;
    ++phases;GuardCheck(current.original_phases[i].has_value());
    const auto* mapped=std::get_if<FlowGuardPhase>(&*current.original_phases[i]);
    GuardCheck(mapped&&mapped->operation==*phase.operation);
    GuardCheck(!FindOriginalPhase(current,{i},phase.phase,phase.operation));
  }
  for(unsigned i=0;i<current.original_exceptions.size();++i) {
    const auto& exit=original.exceptional_exits[i];if(!original.operations[exit.op.value].initialized_guard)continue;
    ++routes;GuardCheck(current.original_exceptions[i].has_value());
    const auto* excluded=std::get_if<FlowExcludedGuardFailure>(&*current.original_exceptions[i]);
    const auto value=FindFlowOriginalGuard(current,exit.op);
    GuardCheck(excluded&&value&&excluded->value==value->expression&&excluded->input==value->entry);
  }
  if(!original.phases.empty())GuardCheck(phases==count*2&&routes==count);
  auto candidate=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  const auto withdrawn=[&] {
    for(unsigned i=0;i<original.operations.size();++i)if(original.operations[i].initialized_guard)
      GuardCheck(!FindFlowOriginalGuard(candidate,{i}));
    for(unsigned i=0;i<original.phases.size();++i)if(const auto op=original.phases[i].operation;op&&original.operations[op->value].initialized_guard)
      GuardCheck(!candidate.original_phases[i]);
    for(unsigned i=0;i<current.original_exceptions.size();++i)if(original.operations[original.exceptional_exits[i].op.value].initialized_guard)
      GuardCheck(!candidate.original_exceptions[i]);
  };
  withdrawn();
  for(auto& transport:transports)transport.reset();
  GuardCheck(!tree.parameters.empty());
  for(unsigned i=0;i<original.operations.size();++i)if(original.operations[i].initialized_guard)
    read_sources[FindFlowOriginalGuard(current,{i})->expression.value]=tree.parameters.front().symbol;
  candidate=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{},std::move(candidate)));withdrawn();
  for(auto& source:read_sources)source.reset();
  auto missing=original;for(auto& operation:missing.operations)operation.initialized_guard.reset();projection.original=&missing;
  candidate=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{},std::move(candidate)));withdrawn();
  auto wrong=original;
  for(auto& operation:wrong.operations)if(operation.initialized_guard)operation.initialized_guard->value=ValueId{static_cast<unsigned>(original.values.size())};
  projection.original=&wrong;candidate=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{},std::move(candidate)));withdrawn();
  projection.original=&original;candidate=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{},std::move(candidate)));
  for(unsigned i=0;i<original.operations.size();++i)if(original.operations[i].initialized_guard)GuardCheck(FindFlowOriginalGuard(candidate,{i}).has_value());
  GuardCheck(std::holds_alternative<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{current.allocated_bytes,current.work})));
  GuardCheck(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{current.allocated_bytes-1,current.work}))==TreeStructureError::kResourceLimit);
  GuardCheck(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{current.allocated_bytes,current.work-1}))==TreeStructureError::kWorkLimit);
  return {current.work,current.allocated_bytes,current.events.size(),current.edges.size()};
}
}
#endif
