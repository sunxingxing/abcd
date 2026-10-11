#ifndef ABCD_TEST_SOURCE_ACTION_REGIONS_H_
#define ABCD_TEST_SOURCE_ACTION_REGIONS_H_
#include "../../../src/source/source_variables.h"
#include "../../../src/source/recovery_projection.h"
#include <source_location>
#include <algorithm>
#include <cstdlib>
#include <iostream>
namespace abcd::test {
inline void RegionCheck(bool value,std::source_location at=std::source_location::current()) {
  if(!value){std::cerr<<"action region check "<<at.line()<<'\n';std::abort();}
}
struct ActionRegionCost {std::uint64_t work=0;std::size_t bytes=0;};
inline ActionRegionCost CheckActionRegions(const source::FunctionTree& tree,const source::OriginalFlow& original) {
  using namespace source;using namespace analysis;
  if(original.action_regions.empty())return {};
  const auto prepared=PrepareSourceVariables(tree,{},&original);
  RegionCheck(std::holds_alternative<SourceVariables>(prepared));
  const auto& plan=std::get<SourceVariables>(prepared).bindings;
  std::vector<FlowValueEquation> equations(tree.expressions.size());std::vector<ExprId> order;
  for(unsigned i=0;i<tree.expressions.size();++i) {
    order.push_back({i});
    if(const auto* read=std::get_if<SymbolExpr>(&tree.expressions[i].expression))equations[i]=FlowBindingValue{read->symbol};
  }
  const auto project=[&](const BindingInitializers& candidate,const OriginalFlow& raw,SemanticBudget budget=SemanticBudget{}) {
    LegacyFlowProjection p{candidate.aliases,candidate.literals,candidate.replacements,candidate.omitted,{}, {},candidate.statements,{},
        candidate.storage,candidate.undefined_stores,candidate.declaration_values,candidate.declaration_completions};
    return PrepareRecoveryProjection(tree,p,{candidate,equations,order,&raw},budget);
  };
  const auto built=project(plan,original);RegionCheck(std::holds_alternative<RecoveryProjection>(built));
  const auto& model=std::get<RecoveryProjection>(built);const auto& flow=model.flow;
  for(unsigned i=0;i<original.action_regions.size();++i) {
    const auto& region=original.action_regions[i];
    RegionCheck(flow.original_regions[i].has_value());
    const auto& action=*flow.original_regions[i];
    RegionCheck(flow.events[action.before.value].read==region.reference);
    const auto result=FindFlowOriginalResult(flow,region.result);RegionCheck(result.has_value());
    RegionCheck(result->expression==region.expression);
    // The source GetValue retains TDZ/effects. No individual member is an
    // upper bound for the whole action, even when the raw load cannot throw.
    RegionCheck(!flow.original_effects[region.expression.value]);
    if(original.phases.empty())continue;
    RegionCheck(result->commit==region.commit&&result->commit_owner==region.exit);
    RegionCheck(FindOriginalPhase(flow,original.operations[region.entry.value].block,ExceptionPhase::kBefore,region.entry)==action.before);
    RegionCheck(FindOriginalPhase(flow,*region.commit,ExceptionPhase::kCommit,region.exit)==action.normal);
    if(region.entry!=region.exit) {
      RegionCheck(!FindOriginalPhase(flow,original.operations[region.exit.value].block,ExceptionPhase::kBefore,region.exit));
      const auto& phase=flow.original_phases[original.operations[region.exit.value].block.value];
      RegionCheck(phase&&std::holds_alternative<FlowRegionPhase>(*phase));
      const auto& value=original.values[original.operations[region.result.value].result->value];
      const auto raw_commit=std::get<OpResult>(value.origin).commit;
      RegionCheck(raw_commit&&!FindOriginalPhase(flow,*raw_commit,ExceptionPhase::kCommit,region.result));
    }
  }
  if(!original.phases.empty())for(unsigned i=0;i<original.exceptional_exits.size();++i) {
    const auto& exit=original.exceptional_exits[i];
    if(FindOriginalActionRegion(original,exit.op))RegionCheck(flow.original_exceptions[i].has_value());
  }
  if(original.action_regions.size()>2)return {model.work,model.allocated_bytes}; // Small mutation oracle.
  auto reversed=original;
  std::reverse(reversed.action_regions.begin(),reversed.action_regions.end());
  for(auto& member:reversed.action_region_members)if(member)member->value=static_cast<unsigned>(reversed.action_regions.size()-1-member->value);
  const auto reordered=project(plan,reversed);RegionCheck(std::holds_alternative<RecoveryProjection>(reordered));
  const auto& reordered_flow=std::get<RecoveryProjection>(reordered).flow;
  for(const auto& region:original.action_regions) {
    const auto result=FindFlowOriginalResult(reordered_flow,region.result);
    RegionCheck(result&&result->expression==region.expression&&result->commit==region.commit);
  }
  const SemanticBudget exact{model.allocated_bytes,model.work};
  RegionCheck(std::holds_alternative<RecoveryProjection>(project(plan,original,exact)));
  RegionCheck(std::get<TreeStructureError>(project(plan,original,{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
  RegionCheck(std::get<TreeStructureError>(project(plan,original,{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
  const auto& region=original.action_regions.front();
  auto raw=original;raw.action_region_members[region.exit.value].reset();
  auto changed=project(plan,raw);RegionCheck(std::holds_alternative<RecoveryProjection>(changed));
  const auto& withdrawn=std::get<RecoveryProjection>(changed).flow;
  RegionCheck(!withdrawn.original_regions.front()&&!FindFlowOriginalResult(withdrawn,region.result));
  if(!original.phases.empty())for(unsigned i=0;i<original.exceptional_exits.size();++i)
    if(original.exceptional_exits[i].op==region.exit)RegionCheck(!withdrawn.original_exceptions[i]);
  if(!original.phases.empty()) {
    raw=original;raw.action_regions.front().commit.reset();
    changed=project(plan,raw);RegionCheck(std::holds_alternative<RecoveryProjection>(changed));
    RegionCheck(!std::get<RecoveryProjection>(changed).flow.original_regions.front());
    RegionCheck(!FindFlowOriginalResult(std::get<RecoveryProjection>(changed).flow,region.result));
  }
  auto candidate=plan;candidate.primitives.push_back(TreeLiteral{UndefinedLiteral{}});
  candidate.read_literals[region.expression.value]=PrimitiveId{static_cast<unsigned>(candidate.primitives.size()-1)};
  changed=project(candidate,original);RegionCheck(std::holds_alternative<RecoveryProjection>(changed));
  RegionCheck(!std::get<RecoveryProjection>(changed).flow.original_regions.front());
  RegionCheck(!FindFlowOriginalResult(std::get<RecoveryProjection>(changed).flow,region.result));
  for(const auto& other:original.action_regions)if(other.reference!=region.reference) {
    candidate=plan;candidate.read_sources[region.expression.value]=other.reference;
    changed=project(candidate,original);RegionCheck(std::holds_alternative<RecoveryProjection>(changed));
    RegionCheck(!std::get<RecoveryProjection>(changed).flow.original_regions.front());
    RegionCheck(!FindFlowOriginalResult(std::get<RecoveryProjection>(changed).flow,region.result));
    break;
  }
  return {model.work,model.allocated_bytes};
}
}  // namespace abcd::test
#endif
