#include "../../../src/source/call_plan.h"
#include "../../../src/source/assignment_values.h"

#include "../../../src/analysis/phased_value_import.h"
#include "analysis/ssa_fixture.h"
#include <cstdlib>
#include <cstdio>
#include <source_location>

namespace {
using namespace abcd::analysis;
using namespace abcd::source;
void Check(bool value, std::source_location where = std::source_location::current()) { if (!value) { std::fprintf(stderr, "assignment_values_test:%u\n", where.line()); std::abort(); } }

class Fixture {
 public:
  Fixture() {
    for (std::uint32_t ordinal = 0; ordinal < 2; ++ordinal)
      graph.values.push_back({EntryDefinition{ParameterEntry{ordinal}}, ValueKinds::Language(), {}});
  }
  OpId Add(ValueOperation operation, bool result,
      std::initializer_list<std::pair<OperandRole, ValueOperand>> inputs) {
    const OpId id{static_cast<std::uint32_t>(graph.ops.size())};
    std::optional<ValueId> value;
    if (result) {
      value = ValueId{static_cast<std::uint32_t>(graph.values.size())};
      graph.values.push_back({OpResult{id}, ValueKinds::Language(), {}});
    }
    graph.origins.push_back({{id.value}, {10 + id.value}, abcd::abc::CodeOffset{100 + id.value}});
    graph.ops.push_back({operation, {0}, id.value,
        {static_cast<std::uint32_t>(graph.uses.size())}, static_cast<std::uint32_t>(inputs.size()),
        value, LanguageEffect::kUserCode, LanguageEffect::kUserCode,
        ExceptionalCompletion::kMayThrow, {id.value}});
    for (const auto& [role, operand] : inputs) {
      const UseId use{static_cast<std::uint32_t>(graph.uses.size())};
      OperandUse occurrence{id, role, ReadPurpose::kOperand, operand, {}, {}, {}, {}};
      if (const auto* input = std::get_if<ValueId>(&operand)) {
        auto& definition = graph.values[input->value];
        occurrence.next = definition.first_use;
        if (occurrence.next) graph.uses[occurrence.next->value].previous = use;
        definition.first_use = use;
      }
      graph.uses.push_back(occurrence);
    }
    return id;
  }
  EvaluationPlan Evaluation() {
    graph.blocks = {{{0}, static_cast<std::uint32_t>(graph.ops.size()), {0}, 0}};
    auto result = BuildEvaluationPlan(graph);
    Check(std::holds_alternative<EvaluationPlan>(result));
    return std::get<EvaluationPlan>(std::move(result));
  }
  ValueProgram graph;
};

std::uint64_t Assignments(std::uint32_t count) {
  Fixture fixture;
  std::vector<OpId> stores;
  for (std::uint32_t i = 0; i < count; ++i) {
    const ValueId rhs{static_cast<std::uint32_t>(fixture.graph.values.size())};
    fixture.graph.values.push_back({EntryDefinition{ParameterEntry{i + 2}}, ValueKinds::Language(), {}});
    const auto read = fixture.Add(ValueNamedRead{{0}}, true, {{OperandRole::kBase, ValueId{0}}});
    const auto store = fixture.Add(ValueNamedWrite{{1}}, false,
        {{OperandRole::kBase, ValueId{1}}, {OperandRole::kStoredValue, rhs}});
    fixture.Add(ValueInvoke{InvokeReceiver::kExplicit, 1}, true,
        {{OperandRole::kCallee, *fixture.graph.ops[read.value].result},
         {OperandRole::kReceiver, ValueId{0}}, {OperandRole::kArgument, rhs}});
    stores.push_back(store);
  }
  const auto plan = fixture.Evaluation();
  Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(fixture.graph, plan)));
  Check(std::holds_alternative<CallPlan>(BuildCallPlan(fixture.graph, plan)));
  for (const auto store : stores) {
    Check(!fixture.graph.ops[store.value].result);
    Check(fixture.graph.ops[store.value].exceptional == ExceptionalCompletion::kMayThrow);
    Check(fixture.graph.ops[store.value].before_throw == LanguageEffect::kUserCode);
    Check(plan.operations[store.value].kind == EvaluationKind::kInline);
    auto forged = plan;
    const auto use = *forged.assignment_consumers[store.value];
    forged.assignment_values[use.value] = OpId{store.value - 1};
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(fixture.graph, forged)));
    forged = plan; forged.assignment_values[use.value].reset();
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(fixture.graph, forged)));
    forged = plan; forged.operations[store.value] = {EvaluationKind::kRoot, {}, {}};
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(fixture.graph, forged)));
    auto changed = fixture.graph;
    changed.uses[use.value].operand = ValueId{0};
    bool refused = false;
    try { TreeCheckBudget check({}); VerifyAssignmentValues(changed, plan, check); }
    catch (EvaluationError error) { refused = error == EvaluationError::kOwnership; }
    Check(refused); // Stored RHS and argument no longer have the same SSA identity.
    if (count > 1) break; // A small mutation oracle; scale test itself is linear.
  }
  SemanticBudget budget; budget.work = plan.work;
  Check(std::holds_alternative<EvaluationPlan>(BuildEvaluationPlan(fixture.graph,budget)));
  --budget.work;
  Check(std::get<EvaluationError>(BuildEvaluationPlan(fixture.graph,budget)) == EvaluationError::kWorkLimit);
  if (count == 1) {
    std::size_t lo=0, hi=1<<20;
    while (lo<hi) { const auto mid=lo+(hi-lo)/2; SemanticBudget b; b.bytes=mid;
      if (std::holds_alternative<EvaluationPlan>(BuildEvaluationPlan(fixture.graph,b))) hi=mid; else lo=mid+1; }
    budget={}; budget.bytes=lo;
    Check(std::holds_alternative<EvaluationPlan>(BuildEvaluationPlan(fixture.graph,budget)));
    --budget.bytes;
    Check(std::get<EvaluationError>(BuildEvaluationPlan(fixture.graph,budget)) == EvaluationError::kResourceLimit);
  }
  return plan.work;
}
void ExternalEvent() {
  Fixture fixture;
  const auto read=fixture.Add(ValueNamedRead{{0}},true,{{OperandRole::kBase,ValueId{0}}});
  const auto store=fixture.Add(ValueNamedWrite{{1}},false,{{OperandRole::kBase,ValueId{0}},{OperandRole::kStoredValue,ValueId{1}}});
  fixture.Add(ValueInvoke{InvokeReceiver::kUndefined,0},true,{{OperandRole::kCallee,ValueId{0}}});
  fixture.Add(ValueInvoke{InvokeReceiver::kExplicit,1},true,{{OperandRole::kCallee,*fixture.graph.ops[read.value].result},{OperandRole::kReceiver,ValueId{0}},{OperandRole::kArgument,ValueId{1}}});
  const auto plan=fixture.Evaluation();
  Check(plan.assignment_values.empty());
  Check(plan.operations[store.value].kind==EvaluationKind::kRoot);
  Check(std::get<CallPlanFailure>(BuildCallPlan(fixture.graph,plan)).kind==CallPlanError::kReference);
}
void MultipleArguments(bool reversed) {
  Fixture fixture;
  fixture.graph.values.push_back({EntryDefinition{ParameterEntry{2}}, ValueKinds::Language(), {}});
  fixture.graph.values.push_back({EntryDefinition{ParameterEntry{3}}, ValueKinds::Language(), {}});
  const auto callee=fixture.Add(ValueNamedRead{{0}},true,{{OperandRole::kBase,ValueId{0}}});
  for(auto v : {reversed?3U:2U,reversed?2U:3U})
    fixture.Add(ValueNamedWrite{{1}},false,{{OperandRole::kBase,ValueId{1}},{OperandRole::kStoredValue,ValueId{v}}});
  const auto call=fixture.Add(ValueInvoke{InvokeReceiver::kExplicit,2},true,
    {{OperandRole::kCallee,*fixture.graph.ops[callee.value].result}, {OperandRole::kReceiver,ValueId{0}},
     {OperandRole::kArgument,ValueId{2}},{OperandRole::kArgument,ValueId{3}}});
  const auto plan=fixture.Evaluation();
  const auto calls=BuildCallPlan(fixture.graph,plan);
  if(reversed) {
    Check(std::holds_alternative<CallPlanFailure>(calls));
    Check(plan.assignment_values.empty());
  } else {
    Check(std::holds_alternative<CallPlan>(calls));
    auto forged=plan;
    const auto first=fixture.graph.ops[call.value].first_use.value+2;
    std::swap(forged.assignment_values[first],forged.assignment_values[first+1]);
    Check(std::holds_alternative<EvaluationError>(VerifyEvaluationPlan(fixture.graph,forged)));
  }
}
void Phased(bool snapshot, bool split, std::uint32_t copies) {
  using namespace abcd;
  const auto op=[](abc::Opcode c){return static_cast<std::uint8_t>(c);};
  constexpr std::uint8_t callee=0, argument=1, receiver=6, rhs=5;
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kMOV_V8_V8),argument,receiver,
    op(abc::Opcode::kLDA_V8),receiver,op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16),0,0,0,
    op(abc::Opcode::kSTA_V8),callee,op(abc::Opcode::kLDA_V8),rhs};
  for(std::uint32_t i=0;i<copies;++i) bytes.insert(bytes.end(),{op(abc::Opcode::kMOV_V8_V8),rhs,rhs});
  bytes.insert(bytes.end(),{op(abc::Opcode::kSTOBJBYNAME_IMM8_ID16_V8),0,1,0,receiver,
    op(abc::Opcode::kSTA_V8),argument});
  if(split) bytes.insert(bytes.end(),{op(abc::Opcode::kJMP_IMM8),2});
  bytes.insert(bytes.end(),{op(abc::Opcode::kLDA_V8),callee,op(abc::Opcode::kCALLTHIS1_IMM8_V8_V8),0,receiver,argument,op(abc::Opcode::kRETURN)});
  const auto handler_pc=static_cast<std::uint32_t>(bytes.size());
  if(snapshot) bytes.insert(bytes.end(),{op(abc::Opcode::kLDA_V8),argument});
  bytes.push_back(op(abc::Opcode::kRETURN));
  auto body=test::Body(bytes);
  abc::TryRegion region; region.offset={0};region.length=handler_pc;region.handler_count=1;
  abc::CatchHandler handler; handler.offset={handler_pc};handler.length=snapshot?3:1;
  body.tries.push_back(region);body.handlers.push_back(handler);
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(3,4,{true,true,false,true}));
  const auto flow=std::get<ExceptionFlow>(BuildExceptionFlow(body,abc::ProfileId::kDynamic1301,frame.registers));
  IdfQuota ssa_work{kDefaultSemanticWork};
  const auto ssa=std::get<ExceptionSsa>(ExceptionSsa::Build(flow,frame.registers,ssa_work));
  std::vector<EntryValueFact> entries;
  for(std::size_t i=0;i<ssa.Values().size();++i){const auto& v=ssa.Values()[i];
    const auto* reg=std::get_if<abc::RegisterId>(&v.slot);
    if(reg && reg->value>=frame.locals && std::holds_alternative<MethodEntry>(v.origin)) entries.push_back({{static_cast<std::uint32_t>(i)},ValueKinds::Language()});}
  const auto env=std::get<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values=std::get<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,entries,value_work,{},PendingExceptionValues::kLanguage));
  const auto proof=std::get<ExceptionInputProof>(VerifyExceptionInputs(flow,ssa,env,values));
  const auto graph=std::get<ValueProgram>(ImportPhasedValues({flow,ssa,env,values,proof,frame}));
  const auto plan=std::get<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
  Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(graph,plan)));
  const auto calls=BuildPhasedCallPlan(graph,plan);
  // Existing corridor projection supports fallthrough phase splitting. A
  // taken-edge jump still needs the general corridor extension; keep it refused.
  if(snapshot || split) Check(std::holds_alternative<CallPlanFailure>(calls));
  else {
    Check(std::holds_alternative<CallPlan>(calls));
    Check(!plan.assignment_values.empty());
    auto forged=plan;
    for(std::size_t i=0;i<forged.assignment_consumers.size();++i) if(forged.assignment_consumers[i]){
      forged.operations[i]={EvaluationKind::kRoot,{}, {}};break;}
    Check(std::holds_alternative<EvaluationError>(VerifyPhasedEvaluationPlan(graph,forged)));
  }
}
} // namespace
int main() {
  std::uint64_t previous=0;
  for (auto n : {1U,200U,201U,400U,800U}) {
    const auto work=Assignments(n);
    if(n>=201) Check(work<=previous*3);
    previous=work;
  }
  MultipleArguments(false); MultipleArguments(true);
  ExternalEvent(); Phased(false,false,0); Phased(false,false,4);
  Phased(false,true,0); Phased(true,false,0);
}
