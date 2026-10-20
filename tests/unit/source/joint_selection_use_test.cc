#include "../../../src/source/joint_selection_use.h"
#include "../../../src/source/call_plan.h"
#include "../../../src/source/tree_checks.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/analysis/value_edits.h"
#include "../../support/analysis/ssa_fixture.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"
using namespace abcd;using namespace abcd::analysis;using namespace abcd::source;
namespace {
constexpr SemanticBudget kBudget{256ULL*1024*1024,100000000};
ValueProgram Graph(unsigned count=1,bool copies=false,bool split=false,unsigned shift=0,bool effect=false,bool width=false) {
  std::vector<std::uint8_t> bytes;
  const auto reg=[&](unsigned n){return static_cast<std::uint8_t>(n+shift);};
  const auto emit=[&](abc::Opcode op){const auto n=static_cast<unsigned>(op);bytes.push_back(n);if(n>UINT8_MAX)bytes.push_back(n>>8);};
  const auto load=[&](unsigned n){emit(abc::Opcode::kLDA_V8);bytes.push_back(reg(n));};
  const auto save=[&](unsigned n){emit(abc::Opcode::kSTA_V8);bytes.push_back(reg(n));};
  const auto branch=[&](abc::Opcode op){const auto pos=bytes.size();emit(op);bytes.push_back(0);return pos;};
  const auto bind=[&](std::size_t pos){test::Check(bytes.size()-pos<INT8_MAX);bytes[pos+1]=static_cast<std::uint8_t>(bytes.size()-pos);};
  const auto corridor=[&]{if(split){emit(abc::Opcode::kJMP_IMM8);bytes.push_back(2);}};
  for(unsigned i=0;i<(width?1:count);++i) {
    load(3);emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16);bytes.insert(bytes.end(),{0,0,0});save(0);
    load(4);emit(abc::Opcode::kISTRUE);const auto no=branch(abc::Opcode::kJEQZ_IMM8);
    emit(abc::Opcode::kLDTRUE);const auto join=branch(abc::Opcode::kJMP_IMM8);
    bind(no);emit(abc::Opcode::kLDFALSE);bind(join);save(1);
    if(copies){emit(abc::Opcode::kMOV_V8_V8);bytes.insert(bytes.end(),{reg(2),reg(1)});load(2);}
    for(unsigned j=0;j<(width?count:1);++j) {
    corridor();load(1);emit(abc::Opcode::kISTRUE);const auto keep=branch(abc::Opcode::kJEQZ_IMM8);
    load(5);const auto end=branch(abc::Opcode::kJMP_IMM8);
    bind(keep);if(effect)emit(abc::Opcode::kDEBUGGER);load(1);bind(end);save(1);corridor();
    }
    load(1);save(2);load(0);emit(abc::Opcode::kCALLTHIS1_IMM8_V8_V8);bytes.insert(bytes.end(),{0,reg(3),reg(2)});
  }
  emit(abc::Opcode::kRETURN);
  const auto body=test::Body(bytes);const auto semantic=std::get<SemanticMethod>(BuildSemanticMethod(body,abc::ProfileId::kDynamic1206,shift+6));
  const auto proof=VerifySemanticSource(body,semantic,shift+6,shift+3,{},CapturedValues::kReject,{},ClosureWrites::kUnknown,{},CallResultValues::kLanguage);
  test::Check(std::holds_alternative<SemanticProof>(proof));
  const auto frame=std::get<abc::FrameLayout>(abc::BuildFrameLayout(shift+3,3,{false,false,false,false}));
  auto imported=ImportNormalValues(body,semantic,std::get<SemanticProof>(proof),frame);test::Check(std::holds_alternative<ValueProgram>(imported));
  auto graph=std::get<ValueProgram>(std::move(imported));test::Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph,graph.revision)));return graph;
}
EvaluationPlan Plan(const ValueProgram& graph,unsigned count) {
  auto result=BuildEvaluationPlan(graph,kBudget);
  if(const auto* e=std::get_if<EvaluationFailure>(&result))std::cerr<<"evaluation error "<<unsigned(e->kind)<<" obligation "<<unsigned(e->evaluation.obligation)<<'\n';
  test::Check(std::holds_alternative<EvaluationPlan>(result));auto plan=std::get<EvaluationPlan>(std::move(result));
  if(plan.joint_selection_uses.size()!=count) std::cerr<<"joint proofs "<<plan.joint_selection_uses.size()<<" expected "<<count<<'\n';
  test::Check(plan.joint_selection_uses.size()==count);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph,plan,kBudget)));
  test::Check(std::holds_alternative<CallPlan>(BuildCallPlan(graph,plan,kBudget)));return plan;
}
void Tree(const ValueProgram& graph,const EvaluationPlan& plan) {
  FunctionSyntax syntax{};syntax.owner={0};syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;
  syntax.names={u"object",u"first",u"second",u"invoke"};
  for(unsigned i=0;i<3;++i)syntax.parameters.push_back({NameId{i},NameEvidence::kDebugRange});
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for(std::size_t i=0;i<graph.ops.size();++i)if(std::holds_alternative<ValueNamedRead>(graph.ops[i].operation))constants[i]=NameId{3};
  ExpressionConstants resolved{constants,syntax.names.size(),0};
  const auto control=std::get<ControlPlan>(BuildControlPlan(graph));
  const auto built=BuildFunctionTree(graph,control,plan,syntax,resolved);test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree=std::get<ConstructedFunction>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph,control,plan,tree,resolved)));
  bool changed=false;
  for(std::size_t i=0;i<tree.expressions.size();++i)if(std::holds_alternative<LogicalExpr>(tree.expressions[i].expression)) {
    auto wrong=tree;auto& logical=std::get<LogicalExpr>(wrong.expressions[i].expression);std::swap(logical.left,logical.right);
    test::Check(!std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph,control,plan,wrong,resolved)));changed=true;
  }
  test::Check(changed);
}
void Mutations() {
  const auto graph=Graph();const auto plan=Plan(graph,1);
  const auto reject=[&](const EvaluationPlan& wrong){test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph,wrong,kBudget)));};
  auto wrong=plan;wrong.joint_selection_uses.clear();reject(wrong);
  wrong=plan;wrong.joint_selection_uses.push_back(wrong.joint_selection_uses.front());reject(wrong);
  wrong=plan;++wrong.joint_selection_uses.front().keep.value;reject(wrong);
  wrong=plan;wrong.joint_selection_uses.front().predicates.clear();reject(wrong);
  wrong=plan;++wrong.joint_selection_uses.front().call.value;reject(wrong);
  wrong=plan;++wrong.joint_selection_uses.front().predicates.front().value;reject(wrong);
  auto different=graph;const auto& proof=plan.joint_selection_uses.front();
  test::Check(!ReplaceUse(different,proof.keep,ValueId{0}));
  auto observed=graph;
  const auto call=proof.call;
  test::Check(!ReplaceUse(observed,UseId{graph.ops[call.value].first_use.value+1},proof.value));
  TreeCheckBudget observed_budget(kBudget);bool observed_rejected=false;
  try{VerifyJointSelectionUses(observed,plan,plan.joint_selection_uses,observed_budget);}catch(TreeStructureError){observed_rejected=true;}test::Check(observed_rejected);
  TreeCheckBudget budget(kBudget);bool rejected=false;
  try{VerifyJointSelectionUses(different,plan,plan.joint_selection_uses,budget);}catch(TreeStructureError){rejected=true;}test::Check(rejected);
  const auto interrupted=Graph(1,false,false,0,true);const auto blocked=std::get<EvaluationPlan>(BuildEvaluationPlan(interrupted,kBudget));
  test::Check(blocked.joint_selection_uses.empty());test::Check(std::holds_alternative<CallPlanFailure>(BuildCallPlan(interrupted,blocked,kBudget)));
}
void Scale() {
  for(const bool width:{false,true}) {
  std::uint64_t previous=0;
  for(unsigned count:{200,201,400,800}) {
    const auto graph=Graph(count,false,false,0,false,width);const auto plan=Plan(graph,count);
    test::Check(std::holds_alternative<EvaluationPlan>(BuildEvaluationPlan(graph,{kBudget.bytes,plan.work})));
    test::Check(std::get<EvaluationFailure>(BuildEvaluationPlan(graph,{kBudget.bytes,plan.work-1}))==EvaluationError::kWorkLimit);
    std::size_t lo=0,hi=kBudget.bytes;
    while(lo+1<hi){const auto mid=lo+(hi-lo)/2;const auto p=BuildEvaluationPlan(graph,{mid,kBudget.work});if(std::holds_alternative<EvaluationPlan>(p))hi=mid;else{test::Check(std::get<EvaluationFailure>(p)==EvaluationError::kResourceLimit);lo=mid;}}
    test::Check(std::holds_alternative<EvaluationPlan>(BuildEvaluationPlan(graph,{hi,kBudget.work})));
    test::Check(std::get<EvaluationFailure>(BuildEvaluationPlan(graph,{hi-1,kBudget.work}))==EvaluationError::kResourceLimit);
    if(previous)test::Check(plan.work<previous*3);previous=plan.work;
    std::cout<<(width?"width ":"calls ")<<count<<" bytes="<<hi<<" work="<<plan.work<<'\n';
  }
  }
}
}
int main(){for(bool copies:{false,true})for(bool split:{false,true})for(unsigned shift:{0,7}){const auto graph=Graph(1,copies,split,shift);const auto plan=Plan(graph,1);if(!shift)Tree(graph,plan);}Mutations();Scale();}
