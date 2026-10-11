#ifndef ABCD_TEST_SOURCE_ALLOCATION_ACTIONS_H_
#define ABCD_TEST_SOURCE_ALLOCATION_ACTIONS_H_
#include "../../../src/source/recovery_flow.h"
#include <cstdlib>
#include <iostream>
#include <source_location>
namespace abcd::test {
inline void AllocationCheck(bool value,std::source_location at=std::source_location::current()) {
  if(!value){std::cerr<<"allocation action check "<<at.line()<<'\n';std::abort();}
}
inline void CheckAllocationActions(const source::FunctionTree& tree,const source::OriginalFlow& original) {
  using namespace source;using namespace analysis;
  std::vector<std::optional<SymbolId>> aliases(tree.symbols.size());
  std::vector<std::optional<PrimitiveId>> literals(tree.symbols.size());
  std::vector<std::optional<ExprId>> replacements(tree.expressions.size());
  std::vector<std::uint8_t> omitted(tree.statements.size()),normal(tree.expressions.size());
  std::vector<ExpressionEvaluation> effects(tree.expressions.size(),ExpressionEvaluation::kRequired);
  LegacyFlowProjection projection{aliases,literals,replacements,omitted,effects,normal};projection.original=&original;
  const auto flow=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  std::vector<const FlowAggregate*> aggregates(tree.expressions.size());
  std::vector<std::uint8_t> carriers(tree.expressions.size());
  for(const auto& carrier:flow.argument_carriers) {
    carriers[carrier.expression.value]=1;
    AllocationCheck(!flow.actions[carrier.expression.value]&&!flow.normal_exits[carrier.expression.value]);
    for(unsigned i=0;i<carrier.properties.count;++i)
      AllocationCheck(!flow.property_initializers[carrier.properties.first+i]);
  }
  std::size_t expected_carriers=0;
  for(const auto& node:tree.expressions)if(const auto* construct=std::get_if<ConstructExpr>(&node.expression))
    if(const auto* arguments=std::get_if<ArrayArguments>(&construct->arguments)) {
      ++expected_carriers;AllocationCheck(carriers[arguments->array.value]);
    }
  AllocationCheck(expected_carriers==flow.argument_carriers.size());
  for(const auto& aggregate:flow.aggregates)aggregates[aggregate.expression.value]=&aggregate;
  std::optional<ExprId> replacement;
  for(unsigned i=0;i<original.operations.size();++i) {
    const auto& operation=original.operations[i];
    if(operation.completion==OriginalCompletionBoundary::kUnmodeled) {
      AllocationCheck(!flow.original_actions[i]&&!FindFlowOriginalResult(flow,OpId{i}));
      AllocationCheck(operation.intrinsic_expression&&!flow.original_effects[operation.intrinsic_expression->value]);
    }
    if(operation.completion!=OriginalCompletionBoundary::kIntrinsicAction)continue;
    AllocationCheck(operation.intrinsic_expression.has_value());const auto expression=*operation.intrinsic_expression;
    if(carriers[expression.value]) {
      AllocationCheck(!flow.original_actions[i]&&!FindFlowOriginalResult(flow,OpId{i}));continue;
    }
    const auto& action=flow.original_actions[i];AllocationCheck(action.has_value());
    AllocationCheck(flow.events[action->before.value].kind==FlowEventKind::kEvaluation);
    if(const auto* aggregate=aggregates[expression.value]) {
      AllocationCheck(action->before==aggregate->allocation&&action->normal==aggregate->allocated);
      if(aggregate->properties.count) {
        AllocationCheck(action->normal!=aggregate->normal);
        if(!replacement)replacement=expression;
      }
    } else AllocationCheck(action->normal==flow.normal_exits[expression.value]);
    const auto value=FindFlowOriginalResult(flow,OpId{i});AllocationCheck(value.has_value());
    AllocationCheck(value->expression==expression);
    if(value->commit) {
      AllocationCheck(FindOriginalPhase(flow,operation.block,ExceptionPhase::kBefore,OpId{i})==action->before);
      AllocationCheck(FindOriginalPhase(flow,*value->commit,ExceptionPhase::kCommit,OpId{i})==action->normal);
    }
  }
  AllocationCheck(std::holds_alternative<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{flow.allocated_bytes,flow.work})));
  AllocationCheck(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{flow.allocated_bytes-1,flow.work}))==TreeStructureError::kResourceLimit);
  AllocationCheck(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{flow.allocated_bytes,flow.work-1}))==TreeStructureError::kWorkLimit);
  if(!replacement)return;
  auto missing=original;
  for(auto& operation:missing.operations)if(operation.intrinsic_expression==replacement)
    operation.completion=OriginalCompletionBoundary::kUnmodeled;
  projection.original=&missing;
  const auto gap=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  for(unsigned i=0;i<missing.operations.size();++i)if(missing.operations[i].intrinsic_expression==replacement) {
    AllocationCheck(!gap.original_actions[i]&&!FindFlowOriginalResult(gap,OpId{i}));
    AllocationCheck(!gap.original_effects[replacement->value]);
  }
  projection.original=&original;
  const auto& aggregate=std::get<ObjectExpr>(tree.expressions[replacement->value].expression);
  replacements[replacement->value]=tree.properties[aggregate.properties.first].value;
  const auto candidate=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  for(unsigned i=0;i<original.operations.size();++i) {
    const auto& operation=original.operations[i];if(operation.intrinsic_expression!=replacement)continue;
    AllocationCheck(!candidate.original_actions[i]&&!candidate.original_effects[replacement->value]);
    AllocationCheck(!FindFlowOriginalResult(candidate,OpId{i}));
    if(const auto value=FindOriginalResult(original,OpId{i});value&&value->commit)
      AllocationCheck(!FindOriginalPhase(candidate,*value->commit,ExceptionPhase::kCommit,OpId{i}));
  }
}
}
#endif
