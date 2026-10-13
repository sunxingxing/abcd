#include "../../../src/source/phase_evaluation.h"
#include "analysis/ssa_fixture.h"
#include <iostream>

namespace {
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
enum class Boundary { kShared, kEffect, kHandler, kMultiple };
std::uint64_t Run(unsigned splits, bool yield, Boundary boundary) {
  ValueProgram graph; EvaluationPlan plan; plan.flow = EvaluationFlow::kPhased;
  graph.values.push_back({EntryDefinition{ParameterEntry{0}}, ValueKinds::Language(), {}});
  const auto block = [&](std::optional<OpId> operation) {
    const auto id = static_cast<std::uint32_t>(graph.blocks.size());
    if (id) { graph.blocks.back().edge_count = 1; graph.edges.push_back({{id-1},{id},FlowEdgeKind::kFallthrough,0}); }
    graph.blocks.push_back({operation.value_or(OpId{static_cast<std::uint32_t>(graph.ops.size())}), operation ? 1U : 0U,
        {static_cast<std::uint32_t>(graph.edges.size())},0});
    graph.phases.push_back({{id},{id},ExceptionPhase::kBefore,operation});
  };
  const auto add = [&](ValueOperation operation, OperandRole role, ValueOperand input, bool effect) {
    const OpId id{static_cast<std::uint32_t>(graph.ops.size())};
    const ValueId value{static_cast<std::uint32_t>(graph.values.size())};
    const UseId use{static_cast<std::uint32_t>(graph.uses.size())};
    graph.values.push_back({OpResult{id},ValueKinds::Language(),{}});
    graph.ops.push_back({operation,{static_cast<std::uint32_t>(graph.blocks.size())},0,use,1,value,
      effect ? LanguageEffect::kUserCode : LanguageEffect::kNone,
      effect ? LanguageEffect::kUserCode : LanguageEffect::kNone,
      effect ? ExceptionalCompletion::kMayThrow : ExceptionalCompletion::kImpossible,{id.value}});
    graph.uses.push_back({id,role,ReadPurpose::kOperand,input,{},{},{},{}});
    if (const auto* operand = std::get_if<ValueId>(&input)) {
      auto& definition = graph.values[operand->value];
      graph.uses.back().next = definition.first_use;
      if (definition.first_use) graph.uses[definition.first_use->value].previous = use;
      definition.first_use = use;
    }
    if(effect)graph.exceptional_exits.push_back({id,{id.value},LanguageEffect::kUserCode,{}});
    plan.operations.push_back({EvaluationKind::kRoot,{},{}});block(id);return id;
  };
  const auto read = add(ValueNamedRead{{0}},OperandRole::kBase,ValueId{0},true);
  for(unsigned i=0;i<splits;++i)block({});
  if(boundary==Boundary::kEffect)add(ValueNamedRead{{1}},OperandRole::kBase,ValueId{0},true);
  const auto suspension=add(yield ? ValueOperation{ValueYield{}} : ValueOperation{ValueAwait{}},
      OperandRole::kInput,*graph.ops[read.value].result,true);
  if(boundary==Boundary::kHandler)graph.exceptional_exits.back().handler=ValueHandler{{},{1}};
  const auto after = add(ValueUnary{UnaryOperator::kTypeOf},OperandRole::kInput,*graph.ops[suspension.value].result,false);
  if(boundary==Boundary::kMultiple)add(ValueUnary{UnaryOperator::kTypeOf},OperandRole::kInput,*graph.ops[read.value].result,false);
  const auto built=BuildPhaseExpressions(graph,plan,{});
  test::Check(std::holds_alternative<PhaseEvaluationCost>(built));
  test::Check(plan.operations[read.value].kind==(boundary==Boundary::kShared ? EvaluationKind::kInline : EvaluationKind::kRoot));
  test::Check(plan.operations[suspension.value].kind==EvaluationKind::kRoot);
  test::Check(std::holds_alternative<PhaseEvaluationCost>(VerifyPhaseExpressions(graph,plan,{})));
  auto moved = plan;
  moved.operations[suspension.value] = {EvaluationKind::kInline, graph.ops[after.value].first_use, {}};
  test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhaseExpressions(graph, moved, {})));
  // A fabricated fusion across an intervening effect, handler change or extra
  // observation must fail the independent interval/identity/catch equations.
  if(boundary!=Boundary::kShared) {
    auto altered=plan;altered.operations[read.value]={EvaluationKind::kInline,graph.ops[suspension.value].first_use,{}};
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhaseExpressions(graph,altered,{})));
  }
  return std::get<PhaseEvaluationCost>(built).work;
}
}
int main() {
  unsigned cases=0;
  for(bool yield:{false,true})for(auto boundary:{Boundary::kShared,Boundary::kEffect,Boundary::kHandler,Boundary::kMultiple}) {
    std::uint64_t previous=0;
    for(unsigned splits:{0U,1U,200U,201U,400U,800U}) {
      const auto work=Run(splits,yield,boundary);
      if(splits>=400)test::Check(work<3*previous);
      previous=work;++cases;
    }
  }
  std::cout<<cases<<" suspension operand forests; effect/catch/use mutations rejected\n";
}
