#ifndef ABCD_TEST_SOURCE_PRESERVED_COMPLETIONS_H_
#define ABCD_TEST_SOURCE_PRESERVED_COMPLETIONS_H_
#include "../../../src/source/source_variables.h"
#include "../../../src/source/recovery_projection.h"
#include "../../../src/source/flow_motion.h"
#include "../../../src/source/definition_elision.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <source_location>
namespace abcd::test {
inline void PreservationCheck(bool good,std::source_location at=std::source_location::current()) {
  if(!good){std::cerr<<"preserved completion check "<<at.line()<<'\n';std::abort();}
}
inline std::size_t CheckPreservedCompletions(const source::FunctionTree& tree,const source::OriginalFlow& original) {
  using namespace source;using namespace analysis;
  std::vector<ExprId> suspension,operands,order;
  std::size_t supercalls=0;
  std::vector<FlowValueEquation> equations(tree.expressions.size());
  for(unsigned i=0;i<tree.expressions.size();++i) {
    order.push_back({i});const auto& node=tree.expressions[i].expression;
    if(const auto* read=std::get_if<SymbolExpr>(&node))equations[i]=FlowBindingValue{read->symbol};
    if(const auto* awaited=std::get_if<AwaitExpr>(&node)){suspension.push_back({i});operands.push_back(awaited->operand);}
    supercalls+=std::holds_alternative<SuperCallExpr>(node);
    if(const auto* yielded=std::get_if<YieldExpr>(&node)){suspension.push_back({i});operands.push_back(yielded->operand);}
  }
  if(suspension.empty()&&!supercalls)return 0;
  const auto variables=std::get<SourceVariables>(PrepareSourceVariables(tree,{},&original));
  const auto& plan=variables.bindings;
  LegacyFlowProjection projection{plan.aliases,plan.literals,plan.replacements,plan.omitted,{},{}};
  projection.loops=&variables.loops;
  const auto check=[&](const OriginalFlow* raw,const BindingInitializers& candidate) {
    const RecoveryExpressionInput input{candidate,equations,order,raw};
    const auto model=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,projection,input));
    const auto& flow=model.flow;
    const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(flow));
    const auto motion=std::get<FlowMotion>(AnalyzeFlowMotion(flow,live));
    const auto demand=std::get<FlowDemand>(AnalyzeFlowDemand(flow));
    const auto elisions=std::get<DefinitionElisions>(PlanDefinitionElisions(flow,demand));
    std::size_t observed=0;
    for(unsigned i=0;i<flow.events.size();++i) {
      const auto& event=flow.events[i];
      const bool paused=event.kind==FlowEventKind::kSuspend;
      const bool super=event.kind==FlowEventKind::kEvaluation&&event.expression&&
          std::holds_alternative<SuperCallExpr>(tree.expressions[event.expression->value].expression);
      if(!paused&&!super)continue;
      ++observed;PreservationCheck(event.expression&&event.effect==ExpressionEvaluation::kRequired);
      const auto owner=flow.expression_owners[event.expression->value];
      if(owner)PreservationCheck(std::find(elisions.statements.begin(),elisions.statements.end(),*owner)==elisions.statements.end());
      if(super) {
        PreservationCheck(event.MayInvokeUserCode(FlowCompletion::kNormal)&&event.MayInvokeUserCode(FlowCompletion::kThrow));
        continue;
      }
      for(const auto completion:{FlowCompletion::kResumeNext,FlowCompletion::kResumeThrow,FlowCompletion::kResumeReturn})
        PreservationCheck(event.MayInvokeUserCode(completion));
      bool next=false,thrown=false,returned=false;
      for(auto edge=event.outgoing;edge;edge=flow.edges[*edge].outgoing) {
        const auto& route=flow.edges[*edge];
        next|=route.completion==FlowCompletion::kResumeNext;
        thrown|=route.completion==FlowCompletion::kResumeThrow;
        returned|=route.completion==FlowCompletion::kResumeReturn;
        // A must-precede fact for a continuation cannot jump over suspension.
        if(motion.previous[route.to.value])PreservationCheck(*motion.previous[route.to.value]==FlowPoint{i});
      }
      PreservationCheck(next&&thrown);
      if(std::holds_alternative<YieldExpr>(tree.expressions[event.expression->value].expression))PreservationCheck(returned);
    }
    PreservationCheck(observed==suspension.size()+supercalls);
    if(raw)for(auto guard:tree.receiver_guards) {
      PreservationCheck(!flow.original_actions[guard.value]&&!FindFlowOriginalResult(flow,guard));
      for(unsigned phase=0;phase<raw->phases.size();++phase)
        if(raw->phases[phase].operation==guard)PreservationCheck(!flow.original_phases[phase]);
    }
  };
  check(&original,plan);
  auto missing=original;
  for(auto& operation:missing.operations)operation.intrinsic_expression.reset();
  check(&missing,plan);check(nullptr,plan);
  // Deliberately over-strong raw ordinary-action caps cannot narrow suspension
  // or its external mutation boundary. This is a consumer counterexample, not
  // an accepted input certificate or a new optimization admission condition.
  auto inert=original;
  for(auto& operation:inert.operations) {
    operation.normal=operation.before_throw=LanguageEffect::kNone;
    operation.exceptional=ExceptionalCompletion::kImpossible;
  }
  check(&inert,plan);
  auto candidate=plan;const PrimitiveId undefined{static_cast<unsigned>(candidate.primitives.size())};
  candidate.primitives.push_back(UndefinedLiteral{});
  for(auto operand:operands)candidate.transports[operand.value]=ExpressionTransport{operand,undefined};
  check(&original,candidate);
  return suspension.size()+supercalls;
}
}
#endif
