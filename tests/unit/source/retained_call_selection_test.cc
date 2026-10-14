#include "../../../src/source/call_plan.h"
#include "../../../src/source/logical_regions.h"
#include "../../../src/source/logical_evaluation.h"
#include "../../../src/source/tree_checks.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/source_variables.h"
#include "../../support/analysis/ssa_fixture.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
#include "../../../src/analysis/value_edits.h"
#include <iostream>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
using test::Check;
namespace {
// o.f(g() || saved): the fallback is an invocation value, not a replayable
// producer in the selected arm. Copies and jump corridors change no proof.
ValueProgram Graph(unsigned count=1, bool copy=false, bool split=false,
    unsigned shift=0, bool member=true) {
  std::vector<std::uint8_t> bytes;
  const auto r=[&](unsigned i){return static_cast<std::uint8_t>(i+shift);};
  for(unsigned n=0;n<count;++n) {
    bytes.insert(bytes.end(),{0x60,r(3),0x42,0,0,0,0x61,r(0)});
    if(split) bytes.insert(bytes.end(),{0x4d,2});
    bytes.insert(bytes.end(),{0x60,r(4),0x29,0});
    if(copy) bytes.insert(bytes.end(),{0x61,r(2),0x60,r(2)});
    bytes.insert(bytes.end(),{0x61,r(1),0xfb,0x13,0,0x51,6,0x60,r(5),0x4d,4,0x60,r(1),0x61,r(2)});
    bytes.insert(bytes.end(),{0x60,r(0)});
    if(member) bytes.insert(bytes.end(),{0x2e,0,r(3),r(2)});
    else bytes.insert(bytes.end(),{0x2a,0,r(2)});
  }
  bytes.push_back(0x64);
  const auto body=test::Body(bytes);
  const auto semantic=BuildSemanticMethod(body,abc::ProfileId::kDynamic1206,shift+6);
  Check(std::holds_alternative<SemanticMethod>(semantic));
  const auto proof=VerifySemanticSource(body,std::get<SemanticMethod>(semantic),shift+6,shift+3,{},CapturedValues::kReject,{},ClosureWrites::kUnknown,{},CallResultValues::kLanguage);
  Check(std::holds_alternative<SemanticProof>(proof));
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(shift+3,3,{false,false,false,false}));
  auto imported=ImportNormalValues(body,std::get<SemanticMethod>(semantic),std::get<SemanticProof>(proof),frame);
  Check(std::holds_alternative<ValueProgram>(imported));auto graph=std::get<ValueProgram>(std::move(imported));
  Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph,graph.revision)));
  return graph;
}
EvaluationPlan Plan(const ValueProgram& graph) {
  auto built=BuildEvaluationPlan(graph);Check(std::holds_alternative<EvaluationPlan>(built));
  auto plan=std::get<EvaluationPlan>(std::move(built));
  Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph,plan)));
  const auto calls=BuildCallPlan(graph,plan);
  Check(std::holds_alternative<CallPlan>(calls));
  return plan;
}
void Variants() {
  for(bool copy:{false,true})for(bool split:{false,true})for(unsigned shift:{0U,7U}) {
    const auto graph=Graph(1,copy,split,shift);const auto plan=Plan(graph);
    unsigned selections=0;
    for(const auto& p:plan.operations) selections+=p.kind==EvaluationKind::kLogicalValue;
    Check(selections==1);
    const auto control=std::get<ControlPlan>(BuildControlPlan(graph));TreeCheckBudget budget({});
    const auto regions=FindSelectionExpressions(graph,control,plan,budget);Check(regions.size()==1);
    Check(regions[0].consequent.has_value());
    Check(std::holds_alternative<SelectionSnapshot>(regions[0].right));
    Check(std::holds_alternative<SelectionSnapshot>(*regions[0].consequent));
    // Keep the existing eager assignment proof, not a new joint-use rule.
    unsigned snapshots=0;for(const auto&p:plan.operations)snapshots+=p.kind==EvaluationKind::kPredicateSnapshot;
    Check(snapshots==1);
  }
  const auto graph=Graph(1,false,false,0,false);const auto plan=Plan(graph);
  for(const auto&p:plan.operations)Check(p.kind!=EvaluationKind::kLogicalValue);
}
void Mutations() {
  auto graph=Graph();const auto plan=Plan(graph);
  OpId branch{},call{},property{},snapshot{};
  for(unsigned i=0;i<graph.ops.size();++i) {
    if(plan.operations[i].kind==EvaluationKind::kLogicalValue)branch={i};
    if(plan.operations[i].kind==EvaluationKind::kPredicateSnapshot)snapshot={i};
    if(auto p=MemberProperty(graph,plan,{i})){call={i};property=*p;}
  }
  auto wrong=plan;wrong.operations[property.value].consumer=graph.ops[call.value].first_use;
  ++wrong.operations[property.value].consumer->value;
  Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph,wrong)));
  wrong=plan;wrong.operations[branch.value].consumer.reset();
  Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph,wrong)));
  wrong=plan;wrong.logical_values.clear();
  Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph,wrong)));
  wrong=plan;wrong.operations[snapshot.value].kind=EvaluationKind::kInline;
  Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph,wrong)));
  // Forged phi value / keep edge cannot be hidden behind a valid placement.
  auto changed=graph;
  for(unsigned i=0;i<changed.uses.size();++i)if(changed.uses[i].life==ValueLife::kLive&&changed.uses[i].role==OperandRole::kPhiInput) {
    Check(!ReplaceUse(changed,{i},*graph.ops[property.value].result));break;
  }
  const auto rejected=VerifyEvaluationPlan(changed,plan);
  Check(std::holds_alternative<EvaluationFailure>(rejected));
  Check(std::get<EvaluationFailure>(rejected).kind!=EvaluationError::kInvalidGraph);
}
void Tree(const ValueProgram& graph,const EvaluationPlan& plan,bool mutations) {
  const auto control=std::get<ControlPlan>(BuildControlPlan(graph));
  FunctionSyntax syntax{};syntax.callable=CallableKind::kFunction;
  syntax.strictness=Strictness::kStrict;syntax.owner={0};
  syntax.names={u"object",u"child",u"fallback",u"call"};
  for(unsigned i=0;i<3;++i)syntax.parameters.push_back({NameId{i},NameEvidence::kDebugRange});
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for(unsigned i=0;i<graph.ops.size();++i)
    if(std::holds_alternative<ValueNamedRead>(graph.ops[i].operation))constants[i]=NameId{3};
  const ExpressionConstants resolved{constants,syntax.names.size(),0};
  const auto built=BuildFunctionTree(graph,control,plan,syntax,resolved);
  Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& function=std::get<ConstructedFunction>(built);
  Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph,control,plan,function.tree,resolved)));
  Check(function.flow&&function.flow->original);
  Check(std::holds_alternative<SourceVariables>(PrepareSourceVariables(function.tree,{},&*function.flow->original)));
  if(!mutations)return;
  const auto refused=[&](const FunctionTree& tree) {
    Check(!std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph,control,plan,tree,resolved)));
  };
  auto missing=function.tree;missing.operations.pop_back();refused(missing);
  auto duplicate=function.tree;duplicate.operations.push_back(duplicate.operations.back());refused(duplicate);
  bool truth=false;
  for(unsigned i=0;i<function.tree.expressions.size();++i)
    if(std::holds_alternative<LogicalExpr>(function.tree.expressions[i].expression)) {
      auto bad=function.tree;auto& logical=std::get<LogicalExpr>(bad.expressions[i].expression);
      logical.operation=logical.operation==TreeLogical::kOr?TreeLogical::kAnd:TreeLogical::kOr;
      refused(bad);truth=true;
      bad=function.tree;
      const auto rhs=std::get<LogicalExpr>(bad.expressions[i].expression).right;
      auto& symbol=std::get<SymbolExpr>(bad.expressions[rhs.value].expression).symbol;
      symbol=SymbolId{symbol.value==0?1U:0U};refused(bad);
    }
  Check(truth);
  auto stale=*function.flow->original;++stale.cfg_revision.value;
  Check(std::get<TreeStructureError>(PrepareSourceVariables(function.tree,{},&stale))==TreeStructureError::kOwnership);
}
void Scale() {
  std::uint64_t previous=0;
  for(unsigned count:{200,201,400,800}) {
    const auto graph=Graph(count,true,true);const auto plan=Plan(graph);
    if(previous)Check(plan.work<previous*3);previous=plan.work;
    Check(std::holds_alternative<EvaluationPlan>(BuildEvaluationPlan(graph,{SemanticBudget{}.bytes,plan.work})));
    Check(std::get<EvaluationFailure>(BuildEvaluationPlan(graph,{SemanticBudget{}.bytes,plan.work-1}))==EvaluationError::kWorkLimit);
    std::size_t lo=0,hi=SemanticBudget{}.bytes;
    while(lo+1<hi) {
      const auto mid=lo+(hi-lo)/2;const auto result=BuildEvaluationPlan(graph,{mid,kDefaultSemanticWork});
      if(std::holds_alternative<EvaluationPlan>(result))hi=mid;
      else {Check(std::get<EvaluationFailure>(result)==EvaluationError::kResourceLimit);lo=mid;}
    }
    Check(std::holds_alternative<EvaluationPlan>(BuildEvaluationPlan(graph,{hi,kDefaultSemanticWork})));
    Check(std::get<EvaluationFailure>(BuildEvaluationPlan(graph,{hi-1,kDefaultSemanticWork}))==EvaluationError::kResourceLimit);
    Tree(graph,plan,false);
    std::cout<<count<<" bytes="<<hi<<" work="<<plan.work<<'\n';
  }
}
}
int main(){Variants();Mutations();const auto graph=Graph();Tree(graph,Plan(graph),true);Scale();std::cout<<"retained_call_selection_test: passed\n";}
