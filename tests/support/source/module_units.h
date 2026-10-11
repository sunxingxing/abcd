#ifndef ABCD_TEST_SOURCE_MODULE_UNITS_H_
#define ABCD_TEST_SOURCE_MODULE_UNITS_H_
#include "../../../src/source/recovery_flow.h"
#include <cstdlib>
#include <iostream>
#include <source_location>
namespace abcd::test {
inline void ModuleUnitCheck(bool good,std::source_location at=std::source_location::current()) {
  if(!good){std::cerr<<"module unit check "<<at.line()<<'\n';std::abort();}
}
struct ModuleUnitCost {std::uint64_t work;std::size_t bytes;};
inline ModuleUnitCost CheckModuleWrites(const source::FunctionTree& tree,const source::OriginalFlow& original,
    std::span<const analysis::OpId> stores,source::SymbolId from,source::SymbolId to) {
  using namespace source;using namespace analysis;
  TreeCheckBudget census_budget({});const auto cells=AnalyzeSourceCells(tree,census_budget);
  ModuleUnitCheck(cells.ModuleUnit(from)&&cells.ModuleUnit(to)&&!cells.SameModuleUnit(from,to));
  // Independent census test: two local aliases can denote one enclosing unit.
  // This is only an identity query, not a claim that a mutated tree retains its
  // old raw proof or is an admissible recovery edit.
  auto renamed=tree;
  for(auto& alias:renamed.modules)if(alias.symbol==to)alias.target=*cells.ModuleUnit(from);
  const auto shared=AnalyzeSourceCells(renamed,census_budget);
  ModuleUnitCheck(shared.SameModuleUnit(from,to));
  std::vector<std::optional<SymbolId>> aliases(tree.symbols.size());
  std::vector<std::optional<PrimitiveId>> literals(tree.symbols.size());
  std::vector<std::optional<ExprId>> replacements(tree.expressions.size());
  std::vector<std::uint8_t> omitted(tree.statements.size()),normal(tree.expressions.size());
  std::vector<ExpressionEvaluation> effects(tree.expressions.size(),ExpressionEvaluation::kRequired);
  LegacyFlowProjection projection{aliases,literals,replacements,omitted,effects,normal};projection.original=&original;
  auto current=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  for(const auto store:stores) {
    ModuleUnitCheck(current.original_actions[store.value].has_value());
    if(!original.phases.empty())ModuleUnitCheck(FindOriginalPhase(current,original.operations[store.value].block,
        ExceptionPhase::kBefore,store).has_value());
  }
  std::vector<bool> writes(original.operations.size());for(const auto store:stores)writes[store.value]=true;
  for(unsigned i=0;i<original.phases.size();++i) {
    const auto& phase=original.phases[i];if(!phase.operation||!writes[phase.operation->value])continue;
    const auto& action=*current.original_actions[phase.operation->value];
    if(phase.phase==ExceptionPhase::kBefore)
      ModuleUnitCheck(FindOriginalPhase(current,WorkBlockId{i},phase.phase,*phase.operation)==action.before);
    if(phase.phase==ExceptionPhase::kCommit)
      ModuleUnitCheck(FindOriginalPhase(current,WorkBlockId{i},phase.phase,*phase.operation)==action.normal);
  }
  const auto check_absent=[&](const RecoveryFlow& flow) {
    for(const auto store:stores)ModuleUnitCheck(!flow.original_actions[store.value]);
    for(unsigned i=0;i<original.phases.size();++i) {
      const auto& phase=original.phases[i];
      if(phase.operation&&writes[phase.operation->value])ModuleUnitCheck(!flow.original_phases[i]);
    }
    for(unsigned i=0;i<flow.original_exceptions.size();++i)
      if(writes[original.exceptional_exits[i].op.value]&&flow.original_exceptions[i])
        ModuleUnitCheck(std::holds_alternative<FlowExcludedResourceFailure>(*flow.original_exceptions[i]));
  };
  // An identical RHS/occurrence cannot certify a write to another external unit.
  aliases[from.value]=to;
  auto candidate=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));check_absent(candidate);
  aliases[from.value].reset();
  candidate=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{},std::move(candidate)));
  for(const auto store:stores)ModuleUnitCheck(candidate.original_actions[store.value].has_value());
  ModuleUnitCheck(std::holds_alternative<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{current.allocated_bytes,current.work})));
  ModuleUnitCheck(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{current.allocated_bytes-1,current.work}))==TreeStructureError::kResourceLimit);
  ModuleUnitCheck(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{current.allocated_bytes,current.work-1}))==TreeStructureError::kWorkLimit);
  return {current.work,current.allocated_bytes};
}
}
#endif
