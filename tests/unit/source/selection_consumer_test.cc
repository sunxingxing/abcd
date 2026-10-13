#include "../../../src/source/selection_consumer.h"
#include "../../../src/source/tree_checks.h"
#include "../../../src/source/evaluation_failure_json.h"
#include "../../../src/source/logical_evaluation.h"
#include "../../support/analysis/ssa_fixture.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include <cstdlib>
#include <iostream>
#include <source_location>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
void Check(bool ok, std::source_location at=std::source_location::current()) {
  if (!ok) { std::cerr << "selection_consumer_test:" << at.line() << '\n'; std::abort(); }
}
ValueProgram Graph() {
  ValueProgram graph;
  graph.ops.resize(2); graph.values.resize(2); graph.uses.resize(1);
  graph.ops[0].operation=ValueReturn{}; graph.ops[0].block={1};
  graph.values[0].first_use=UseId{0};
  graph.uses[0]={OpId{0},OperandRole::kReturnValue,ReadPurpose::kOperand,ValueId{0},{},{},{},{}};
  return graph;
}
SelectionRegion Region() { SelectionRegion region{}; region.merged={0};region.join={1};return region; }
SelectionConsumer Find(const ValueProgram& graph, const SelectionRegion* next=nullptr) {
  TreeCheckBudget budget({1000000,1000000}); return FindSelectionConsumer(graph,Region(),budget,next);
}
void Classification() {
  auto graph=Graph(); auto exit=Find(graph);
  Check(exit.kind==SelectionExitKind::kUnique && exit.InlineUse()==UseId{0});
  graph.uses.push_back(graph.uses[0]);graph.uses[0].next=UseId{1};graph.uses[1].owner=OpId{1};graph.ops[1].block={1};
  exit=Find(graph);Check(!exit.InlineUse() && exit.reason==SelectionExitReason::kMultipleUses && exit.witness==UseId{1});
  graph=Graph();graph.ops[0].block={2};exit=Find(graph);
  Check(exit.reason==SelectionExitReason::kCrossBlock && exit.witness==UseId{0});
  graph=Graph();graph.uses[0].owner=ValueId{1};graph.uses[0].incoming=EdgeId{7};exit=Find(graph);
  Check(exit.reason==SelectionExitReason::kPhiUse && exit.edge==EdgeId{7});
  graph=Graph();graph.uses[0].role=OperandRole::kReceiver;Check(Find(graph).reason==SelectionExitReason::kProtocolRole);
  graph=Graph();graph.values[0].first_use.reset();Check(Find(graph).reason==SelectionExitReason::kNoUse);
  graph=Graph();graph.ops[0].operation=ValueBranch{UnaryOperator::kFalseOrZero};graph.uses[0].role=OperandRole::kCondition;
  Check(Find(graph).kind==SelectionExitKind::kSnapshot);
}
void KeepEdge() {
  auto graph=Graph();graph.ops[0].operation=ValueUnary{UnaryOperator::kToBoolean};graph.uses[0].role=OperandRole::kInput;
  graph.uses[0].next=UseId{1};
  graph.uses.push_back({ValueId{1},OperandRole::kPhiInput,ReadPurpose::kOperand,ValueId{0},EdgeId{4},{},UseId{0},{}});
  SelectionRegion next{};next.boolean={0};next.merged={1};next.keep_input=EdgeId{4};
  auto exit=Find(graph,&next);
  Check(exit.kind==SelectionExitKind::kPredicateKeep && exit.InlineUse()==UseId{0} && exit.keep==UseId{1} && exit.edge==EdgeId{4});
  next.keep_input=EdgeId{5};Check(Find(graph,&next).reason==SelectionExitReason::kKeepEdge);
  next.keep_input=EdgeId{4};graph.uses[1].owner=ValueId{0};Check(Find(graph,&next).reason==SelectionExitReason::kPhiUse);
  graph.uses[1].owner=ValueId{1};graph.uses[1].life=ValueLife::kRemoved;Check(Find(graph,&next).reason==SelectionExitReason::kKeepEdge);
  graph.uses[1].life=ValueLife::kLive;

}
void Scale() {
  for (const std::uint32_t count : {200,201,400,800}) {
    auto graph=Graph();graph.uses.resize(count);
    // Removed occurrences must neither prevent fusion nor escape the budget.
    for(std::uint32_t i=0;i<count;++i) {graph.uses[i].life=i==count-1?ValueLife::kLive:ValueLife::kRemoved;graph.uses[i].next=i+1<count?std::optional{UseId{i+1}}:std::nullopt;}
    graph.uses.back().owner=OpId{0};graph.uses.back().role=OperandRole::kReturnValue;
    TreeCheckBudget exact({0,count});auto result=FindSelectionConsumer(graph,Region(),exact);
    Check(result.InlineUse()==UseId{count-1} && exact.Work()==count && exact.Bytes()==0);
    TreeCheckBudget short_budget({0,count-1});bool rejected=false;
    try { (void)FindSelectionConsumer(graph,Region(),short_budget); }
    catch(TreeStructureError error) { rejected=error==TreeStructureError::kWorkLimit; }
    Check(rejected);
  }
}
// A selection chain whose final value has two real observations. Transparent
// copies and split jump corridors change neither the observation set nor the
// requirement to keep the final phi. Opcode bytes are fixture inputs only.
ValueProgram Boundary(unsigned count, bool copies, bool split) {
  std::vector<std::uint8_t> bytes;
  for (unsigned i=0;i<count;++i) {
    bytes.insert(bytes.end(), {0x60, 2}); // parameter
    if(copies) bytes.insert(bytes.end(),{0x61,1,0x60,1});
    bytes.insert(bytes.end(), {0xfb,0x13,0,0x51,5,0x02,0x4d,3,0x03,0x61,0});
    if(split) bytes.insert(bytes.end(),{0x4d,2});
    bytes.insert(bytes.end(), {0xfb,0x14,0,0x51,6,0x60,2,0x4d,4,0x60,0,0x61,0});
    // Observable rereads of the result; no duplicated producer is admissible.
    bytes.insert(bytes.end(), {0x60,0,0x42,0,0,0});
  }
  bytes.insert(bytes.end(),{0x60,0,0x64});
  const auto body=test::Body(bytes);
  auto semantic=BuildSemanticMethod(body,abc::ProfileId::kDynamic1206,3);
  Check(std::holds_alternative<SemanticMethod>(semantic));
  auto proof=VerifySemanticSource(body,std::get<SemanticMethod>(semantic),2,1);
  Check(std::holds_alternative<SemanticProof>(proof));
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(2,1,{false,false,false,false}));
  auto imported=ImportNormalValues(body,std::get<SemanticMethod>(semantic),std::get<SemanticProof>(proof),frame);
  Check(std::holds_alternative<ValueProgram>(imported));auto graph=std::get<ValueProgram>(std::move(imported));
  Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph,graph.revision)));
  return graph;
}
void Routes() {
  for(const bool copies:{false,true}) for(const bool split:{false,true}) {
    auto graph=Boundary(1,copies,split);auto built=BuildEvaluationPlan(graph);
    Check(std::holds_alternative<EvaluationPlan>(built));const auto& plan=std::get<EvaluationPlan>(built);
    Check(!std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph,plan)));
    const auto control=std::get<ControlPlan>(BuildControlPlan(graph));TreeCheckBudget budget({});
    const auto regions=FindSelectionExpressions(graph,control,plan,budget);
    bool tested=false;
    for(const auto& region:regions) {
      if(region.parent||!region.complete_phi)continue;
      const auto exit=FindSelectionConsumer(graph,region,budget);
      if(exit.InlineUse())continue;
      auto forged=plan;
      forged.logical_values.resize(graph.values.size());forged.logical_values[region.merged.value]=region.branch;
      forged.operations[region.branch.value]={EvaluationKind::kLogicalValue,graph.ops[region.boolean.value].first_use,{}};
      Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph,forged)));
      TreeCheckBudget independent({});bool located=false;
      try {VerifyLogicalValues(graph,forged,independent);}
      catch(const EvaluationFailure& failure) {
        located=failure.kind==EvaluationError::kOwnership && failure.evaluation.selection_exit==exit.reason &&
            failure.evaluation.use==exit.witness && failure.evaluation.location.operation==region.branch;
      }
      Check(located);tested=true;
    }
    Check(tested);
  }
  std::uint64_t previous=0;
  for(unsigned count:{200,201,400,800}) {
    auto graph=Boundary(count,true,true);auto built=BuildEvaluationPlan(graph);
    Check(std::holds_alternative<EvaluationPlan>(built));const auto& plan=std::get<EvaluationPlan>(built);
    if(previous) Check(plan.work<previous*3);previous=plan.work;
    Check(!std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph,plan)));
    SemanticBudget exact{};exact.work=plan.work;
    Check(std::holds_alternative<EvaluationPlan>(BuildEvaluationPlan(graph,exact)));
    --exact.work;Check(std::get<EvaluationFailure>(BuildEvaluationPlan(graph,exact))==EvaluationError::kWorkLimit);
    exact={0,kDefaultSemanticWork};Check(std::get<EvaluationFailure>(BuildEvaluationPlan(graph,exact))==EvaluationError::kResourceLimit);
    // Locate the exact admission boundary without assuming that retained-plan
    // storage equals transient peak scratch. This deliberately slow test oracle
    // is separate from the linear production pass.
    std::size_t lo=0,hi=SemanticBudget{}.bytes;
    while(lo+1<hi) {
      const auto mid=lo+(hi-lo)/2;
      auto candidate=BuildEvaluationPlan(graph,{mid,kDefaultSemanticWork});
      if(std::holds_alternative<EvaluationPlan>(candidate)) hi=mid;
      else {Check(std::get<EvaluationFailure>(candidate)==EvaluationError::kResourceLimit);lo=mid;}
    }
    Check(std::holds_alternative<EvaluationPlan>(BuildEvaluationPlan(graph,{hi,kDefaultSemanticWork})));
    Check(std::get<EvaluationFailure>(BuildEvaluationPlan(graph,{hi-1,kDefaultSemanticWork}))==EvaluationError::kResourceLimit);
  }
}
void Json() {
  for(unsigned i=0;i<=static_cast<unsigned>(SelectionExitReason::kKeepEdge);++i) {
    EvaluationEvidence evidence; evidence.selection_exit=static_cast<SelectionExitReason>(i);
    evidence.selection_edge=EdgeId{0};evidence.use=UseId{0};evidence.actual_owner=TreeLocation{};evidence.actual_owner->operation=OpId{0};
    Check(ParseEvaluationFailure(EvaluationFailureJson(evidence))==evidence);
  }
}
}
int main() {Classification();KeepEdge();Scale();Routes();Json();}
