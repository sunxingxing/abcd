#include "../../../src/source/call_plan.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/analysis/value_edits.h"
#include "analysis/ssa_fixture.h"
#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"

using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
constexpr SemanticBudget kTestBudget{256ULL * 1024 * 1024, 100000000};
// A member reference, followed by separately selected argument subtrees with
// intervening coercions. Copies, register permutations and split corridors are
// generated independently of the production def/use and interval proof.
ValueProgram Graph(unsigned width, unsigned calls = 1, bool copies = false,
                   bool split = false, unsigned shift = 0, bool barrier = false) {
  std::vector<std::uint8_t> code;
  const auto reg = [&](unsigned n) { return static_cast<std::uint8_t>(n + shift); };
  const auto emit = [&](abc::Opcode op) {
    const auto value = static_cast<unsigned>(op);
    code.push_back(static_cast<std::uint8_t>(value));
    if (value > UINT8_MAX) code.push_back(static_cast<std::uint8_t>(value >> 8));
  };
  const auto load = [&](unsigned n) { emit(abc::Opcode::kLDA_V8); code.push_back(reg(n)); };
  const auto save = [&](unsigned n) { emit(abc::Opcode::kSTA_V8); code.push_back(reg(n)); };
  const auto corridor = [&] { if (split) { emit(abc::Opcode::kJMP_IMM8); code.push_back(2); } };
  for (unsigned n = 0; n < calls; ++n) {
    load(4); emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); code.insert(code.end(), {0,0,0}); save(0);
    emit(abc::Opcode::kLDAI_IMM32); code.insert(code.end(), {1,0,0,0}); save(2);
    for (unsigned i = 0; i < width; ++i) {
      corridor(); load(5); emit(abc::Opcode::kCALLARG0_IMM8); code.push_back(0); save(1);
      if (copies) { emit(abc::Opcode::kMOV_V8_V8); code.insert(code.end(), {reg(3),reg(1)}); load(3); }
      const auto branch = [&](abc::Opcode op) {
        const auto at=code.size();emit(op);code.push_back(0);return at;
      };
      const auto bind = [&](std::size_t at) {
        test::Check(code.size()-at<INT8_MAX);code[at+1]=static_cast<std::uint8_t>(code.size()-at);
      };
      emit(abc::Opcode::kLDNULL);emit(abc::Opcode::kSTRICTEQ_IMM8_V8);code.insert(code.end(),{0,reg(1)});
      const auto null=branch(abc::Opcode::kJNEZ_IMM8);
      emit(abc::Opcode::kLDUNDEFINED);emit(abc::Opcode::kSTRICTEQ_IMM8_V8);code.insert(code.end(),{0,reg(1)});
      const auto undefined=branch(abc::Opcode::kJNEZ_IMM8);
      const auto read=branch(abc::Opcode::kJMP_IMM8);
      bind(null);bind(undefined);emit(abc::Opcode::kLDUNDEFINED);
      const auto join=branch(abc::Opcode::kJMP_IMM8);
      bind(read);load(1);emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16);code.insert(code.end(),{0,0,0});
      bind(join);
      save(3); load(3);
      emit(abc::Opcode::kADD2_IMM8_V8); code.insert(code.end(), {0,reg(2)}); save(2); corridor();
    }
    if (barrier) emit(abc::Opcode::kDEBUGGER);
    load(0); emit(abc::Opcode::kCALLTHIS1_IMM8_V8_V8); code.insert(code.end(), {0,reg(4),reg(2)});
  }
  emit(abc::Opcode::kRETURN);
  const auto body = test::Body(code);
  const auto semantic = std::get<SemanticMethod>(BuildSemanticMethod(body,abc::ProfileId::kDynamic1206,shift+7));
  const auto proof = VerifySemanticSource(body,semantic,shift+7,shift+4,{},CapturedValues::kReject,
      {},ClosureWrites::kUnknown,{},CallResultValues::kLanguage,FrameReceiver::kUnknown,nullptr,ArithmeticSemantics::kSourceLanguage);
  test::Check(std::holds_alternative<SemanticProof>(proof));
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(shift+4,3,{false,false,false,false}));
  auto imported = ImportNormalValues(body,semantic,std::get<SemanticProof>(proof),frame);
  test::Check(std::holds_alternative<ValueProgram>(imported));
  auto graph = std::get<ValueProgram>(std::move(imported));
  test::Check(!std::holds_alternative<ValueSimplifyError>(SimplifyValues(graph,graph.revision)));
  return graph;
}
EvaluationPlan Plan(const ValueProgram& graph, unsigned expected) {
  auto built = BuildEvaluationPlan(graph,kTestBudget);
  if (auto* e=std::get_if<EvaluationFailure>(&built)) std::cerr<<"evaluation "<<unsigned(e->kind)<<'\n';
  test::Check(std::holds_alternative<EvaluationPlan>(built));
  auto plan=std::get<EvaluationPlan>(std::move(built));
  if(plan.logical_reference_routes.size()!=expected) {
    std::cerr<<"routes "<<plan.logical_reference_routes.size()<<" expected "<<expected<<"\n";
    for(unsigned i=0;i<graph.ops.size();++i)std::cerr<<i<<" b"<<graph.ops[i].block.value<<" kind"<<unsigned(plan.operations[i].kind)<<" temp"<<(plan.operations[i].temporary?int(*plan.operations[i].temporary):-1)<<" use"<<(plan.operations[i].consumer?int(plan.operations[i].consumer->value):-1)<<"\n";
  }
  test::Check(plan.logical_reference_routes.size()==expected);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyEvaluationPlan(graph,plan,kTestBudget)));
  test::Check(std::holds_alternative<CallPlan>(BuildCallPlan(graph,plan,kTestBudget)));
  return plan;
}
void Mutations() {
  const auto graph=Graph(3);const auto plan=Plan(graph,1);
  const auto rejected=[&](const EvaluationPlan& p){test::Check(std::holds_alternative<EvaluationFailure>(VerifyEvaluationPlan(graph,p)));};
  auto wrong=plan;wrong.logical_reference_routes.clear();rejected(wrong);
  wrong=plan;wrong.logical_reference_routes.front().call={UINT32_MAX};rejected(wrong);
  wrong=plan;wrong.logical_reference_routes.push_back(wrong.logical_reference_routes.front());rejected(wrong);
  wrong=plan;wrong.logical_reference_routes.front().branches.pop_back();rejected(wrong);
  wrong=plan;std::swap(wrong.logical_reference_routes.front().branches[0],wrong.logical_reference_routes.front().branches[1]);rejected(wrong);
  wrong=plan;wrong.logical_reference_routes.front().call={0};rejected(wrong);
  const auto call=plan.logical_reference_routes.front().call;
  const auto property=*MemberProperty(graph,plan,call);
  wrong=plan;++wrong.operations[property.value].consumer->value;rejected(wrong);
  for(const auto branch:plan.logical_reference_routes.front().branches) {
    wrong=plan;wrong.operations[branch.value].consumer.reset();rejected(wrong);
  }
  auto changed=graph;
  test::Check(!ReplaceUse(changed,{graph.ops[call.value].first_use.value+1},*graph.ops[property.value].result));
  test::Check(std::holds_alternative<CallPlanFailure>(BuildCallPlan(changed,plan)));
  // An unowned observable event cannot be swallowed by any claimed route.
  const auto interrupted=Graph(3,1,false,false,0,true);
  const auto blocked=std::get<EvaluationPlan>(BuildEvaluationPlan(interrupted));
  test::Check(blocked.logical_reference_routes.empty());
  test::Check(std::holds_alternative<CallPlanFailure>(BuildCallPlan(interrupted,blocked)));
}
void Tree(const ValueProgram& graph,const EvaluationPlan& plan) {
  FunctionSyntax syntax{};syntax.owner={0};syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;
  syntax.names={u"object",u"child",u"fallback",u"method"};
  for(unsigned i=0;i<3;++i)syntax.parameters.push_back({NameId{i},NameEvidence::kDebugRange});
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for(unsigned i=0;i<graph.ops.size();++i)if(std::holds_alternative<ValueNamedRead>(graph.ops[i].operation))constants[i]=NameId{3};
  const ExpressionConstants resolved{constants,syntax.names.size(),0};
  const auto control=std::get<ControlPlan>(BuildControlPlan(graph));
  auto built=BuildFunctionTree(graph,control,plan,syntax,resolved);
  test::Check(std::holds_alternative<ConstructedFunction>(built));
  const auto& tree=std::get<ConstructedFunction>(built).tree;
  test::Check(std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph,control,plan,tree,resolved)));
  const auto refuse = [&](const FunctionTree& changed) {
    test::Check(!std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph,control,plan,changed,resolved)));
  };
  auto wrong=tree;wrong.operations.pop_back();refuse(wrong);
  wrong=tree;wrong.operations.push_back(wrong.operations.back());refuse(wrong);
  bool call_changed=false, arm_changed=false;
  for (std::size_t i=0;i<tree.expressions.size();++i) {
    if (const auto* call=std::get_if<CallExpr>(&tree.expressions[i].expression);
        call && call->reference==CallReference::kMember) {
      wrong=tree;std::get<CallExpr>(wrong.expressions[i].expression).reference=CallReference::kValue;
      refuse(wrong);call_changed=true;
    }
    if (std::holds_alternative<ConditionalExpr>(tree.expressions[i].expression)) {
      wrong=tree;auto& branch=std::get<ConditionalExpr>(wrong.expressions[i].expression);
      std::swap(branch.consequent,branch.alternate);refuse(wrong);arm_changed=true;
    }
  }
  test::Check(call_changed && arm_changed);
}
void Scale() {
  for(const bool width:{false,true}) {
    std::uint64_t previous_work=0;std::size_t previous_bytes=0;
    for(const unsigned n:{200,201,400,800}) {
      const auto graph=Graph(width?n:2,width?1:n);
      const auto plan=Plan(graph,width?1:n);
      const auto build=[&](SemanticBudget b){return BuildEvaluationPlan(graph,b);};
      test::Check(std::holds_alternative<EvaluationPlan>(build({kTestBudget.bytes,plan.work})));
      test::Check(std::get<EvaluationFailure>(build({kTestBudget.bytes,plan.work-1}))==EvaluationError::kWorkLimit);
      std::size_t lo=0,hi=kTestBudget.bytes;
      while(lo+1<hi) {
        const auto mid=lo+(hi-lo)/2;const auto result=build({mid,kTestBudget.work});
        if(std::holds_alternative<EvaluationPlan>(result))hi=mid;
        else {test::Check(std::get<EvaluationFailure>(result)==EvaluationError::kResourceLimit);lo=mid;}
      }
      test::Check(std::holds_alternative<EvaluationPlan>(build({hi,kTestBudget.work})));
      test::Check(std::get<EvaluationFailure>(build({hi-1,kTestBudget.work}))==EvaluationError::kResourceLimit);
      if(previous_work)test::Check(plan.work<3*previous_work && hi<3*previous_bytes);
      previous_work=plan.work;previous_bytes=hi;
      std::cout<<(width?"width ":"calls ")<<n<<" bytes="<<hi<<" work="<<plan.work<<'\n';
    }
  }
}
}
int main() {
  for(const bool copies:{false,true})for(const bool split:{false,true})for(const unsigned shift:{0,7}) {
    const auto graph=Graph(3,1,copies,split,shift);const auto plan=Plan(graph,1);
    if(!shift)Tree(graph,plan);
  }
  Mutations();Scale();
}
