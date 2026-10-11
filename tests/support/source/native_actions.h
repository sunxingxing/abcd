#ifndef ABCD_TEST_SOURCE_NATIVE_ACTIONS_H_
#define ABCD_TEST_SOURCE_NATIVE_ACTIONS_H_
#include "../../../src/source/recovery_flow.h"
#include <cstdlib>
#include <iostream>
#include <source_location>
namespace abcd::test {
inline void NativeCheck(bool value,std::source_location at=std::source_location::current()) {
  if(!value){std::cerr<<"native action check "<<at.line()<<'\n';std::abort();}
}
// Checked source primitives are the independent inventory; counting only raw
// certificates would silently pass if a whole operation category were omitted.
inline void CheckNativeActions(const source::FunctionTree& tree,const source::OriginalFlow& original) {
  using namespace source;using namespace analysis;
  std::vector<std::optional<SymbolId>> aliases(tree.symbols.size());
  std::vector<std::optional<PrimitiveId>> literals(tree.symbols.size());
  std::vector<std::optional<ExprId>> replacements(tree.expressions.size());
  std::vector<std::uint8_t> omitted(tree.statements.size()),normal(tree.expressions.size());
  std::vector<std::optional<ExpressionTransport>> transport(tree.expressions.size());
  std::vector<ExpressionEvaluation> effects(tree.expressions.size(),ExpressionEvaluation::kRequired);
  LegacyFlowProjection projection{aliases,literals,replacements,omitted,effects,normal};projection.original=&original;
  projection.transports=transport;
  const auto flow=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  std::vector<std::optional<OpId>> operations(tree.expressions.size());
  for(unsigned i=0;i<original.operations.size();++i) {
    const auto& operation=original.operations[i];
    if(operation.intrinsic_expression)operations[operation.intrinsic_expression->value]=OpId{i};
  }
  std::size_t count=0;
  for(unsigned i=0;i<tree.expressions.size();++i) {
    const auto& node=tree.expressions[i].expression;
    const bool function=std::holds_alternative<FunctionExpr>(node),klass=std::holds_alternative<ClassExpr>(node);
    if(!function&&!klass&&!std::holds_alternative<CallExpr>(node)&&!std::holds_alternative<ConstructExpr>(node))continue;
    NativeCheck(operations[i].has_value());const auto id=*operations[i];const auto& raw=original.operations[id.value];
    const auto& action=flow.original_actions[id.value];NativeCheck(action.has_value());
    NativeCheck(flow.events[action->before.value].kind==FlowEventKind::kEvaluation);
    NativeCheck(action->normal==flow.normal_exits[i]);
    if(FindOriginalResult(original,id))NativeCheck(FindFlowOriginalResult(flow,id).has_value());
    NativeCheck(flow.original_effects[i].has_value());
    if(klass)NativeCheck(HasLanguageEffect(flow.original_effects[i]->normal,LanguageEffect::kUserCode));
    if(function)NativeCheck(HasLanguageEffect(flow.original_effects[i]->normal,LanguageEffect::kAllocation));
    if(!original.phases.empty()) {
      NativeCheck(FindOriginalPhase(flow,raw.block,ExceptionPhase::kBefore,id)==action->before);
      const auto result=FindOriginalResult(original,id);NativeCheck(result&&result->commit);
      NativeCheck(FindOriginalPhase(flow,*result->commit,ExceptionPhase::kCommit,id)==action->normal);
    }
    transport[i]=ExpressionTransport{ExprId{i},PrimitiveId{0}};++count;
  }
  if(!count)return;
  // Removing the physical primitives is deliberately not an equivalent edit.
  // It cannot inherit their type/effect/phase or captured-unit observations.
  const auto candidate=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  for(unsigned i=0;i<transport.size();++i)if(transport[i]) {
    const auto id=*operations[i];NativeCheck(!candidate.original_actions[id.value]);
    NativeCheck(!candidate.original_effects[i]&&!FindFlowOriginalResult(candidate,id));
    if(!original.phases.empty()) {
      const auto& raw=original.operations[id.value];
      NativeCheck(!FindOriginalPhase(candidate,raw.block,ExceptionPhase::kBefore,id));
      if(const auto result=FindOriginalResult(original,id);result&&result->commit)
        NativeCheck(!FindOriginalPhase(candidate,*result->commit,ExceptionPhase::kCommit,id));
    }
  }
  for(const auto& capture:candidate.unit_captures) {
    const auto expression=candidate.events[capture.point.value].expression;
    NativeCheck(expression.has_value());NativeCheck(!transport[expression->value]);
  }
}
}
#endif
