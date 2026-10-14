#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_checks.h"
#include "../../../src/source/tree_structure.h"
#include "../../../src/source/tree_edge_flow.h"
#include "../../../src/source/completion_tree.h"
#include "../../../src/source/recovery_flow.h"
#include "../../support/analysis/ssa_fixture.h"
#include "abcd/source/tree_printer.h"
#include <iostream>
#include <sstream>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
using test::Check;
namespace {
template<class T,class V>T Take(V v,std::source_location at=std::source_location::current()){
 if(!std::holds_alternative<T>(v)){std::cerr<<at.line()<<" variant "<<v.index()<<'\n';std::visit([](const auto& x){if constexpr(requires{x.kind;})std::cerr<<unsigned(x.kind)<<'\n';},v);std::abort();}return std::get<T>(std::move(v));
}
abc::MethodBody Body(unsigned count,bool reverse,bool copies,bool split){
 std::vector<std::uint8_t> bytes;
 const auto emit=[&](abc::Opcode op){bytes.push_back(static_cast<std::uint8_t>(op));};
 const auto load=[&](unsigned r){emit(abc::Opcode::kLDA_V8);bytes.push_back(r);};
 const auto save=[&](unsigned r){emit(abc::Opcode::kSTA_V8);bytes.push_back(r);};
 const auto read=[&](unsigned key){load(2);emit(abc::Opcode::kLDOBJBYNAME_IMM8_ID16);bytes.insert(bytes.end(),{0,static_cast<std::uint8_t>(key),0});};
 const auto jump=[&](abc::Opcode op){auto pc=bytes.size();emit(op);bytes.push_back(0);return pc;};
 const auto patch=[&](std::size_t pc){Check(bytes.size()-pc<128);bytes[pc+1]=bytes.size()-pc;};
 for(unsigned i=0;i<count;++i){
  read(0);save(0);
  if(copies){emit(abc::Opcode::kMOV_V8_V8);bytes.insert(bytes.end(),{1,0});emit(abc::Opcode::kMOV_V8_V8);bytes.insert(bytes.end(),{0,1});}
  read(1);emit(abc::Opcode::kISTRUE);
  const auto shared=jump(abc::Opcode::kJNEZ_IMM8);
  if(split){emit(abc::Opcode::kJMP_IMM8);bytes.push_back(2);}
  read(2);emit(abc::Opcode::kISTRUE);
  const auto end=jump(reverse?abc::Opcode::kJNEZ_IMM8:abc::Opcode::kJEQZ_IMM8);
  patch(shared);read(3);save(0);read(4);
  patch(end);load(0);emit(abc::Opcode::kSTOBJBYNAME_IMM8_ID16_V8);bytes.insert(bytes.end(),{0,5,0,2});load(0);
 }
 emit(abc::Opcode::kRETURN);return test::Body(bytes);
}
std::pair<std::uint64_t,std::size_t> Run(unsigned count,bool reverse=false,bool copies=false,bool split=false,bool print=false){
 const auto body=Body(count,reverse,copies,split);
 const auto frame=Take<abc::FrameLayout>(abc::BuildFrameLayout(2,1,{}));
 RouteInputIdentity identity;identity.profile=abc::ProfileId::kDynamic1206;
 auto input=Take<FunctionInput>(FunctionInput::Build(identity,{300},{100},body,frame,InputCaptureMode::kClosed));
 auto values=Take<FunctionValues>(BuildFunctionValues(input,body,frame));auto& graph=values.graph;
 if(reverse)for(auto& op:graph.ops)if(auto* branch=std::get_if<ValueBranch>(&op.operation)){auto& block=graph.blocks[op.block.value];std::swap(graph.edges[block.first_edge.value].target,graph.edges[block.first_edge.value+1].target);for(auto& use:graph.uses)if(use.incoming)if(auto* edge=std::get_if<EdgeId>(&*use.incoming)){if(edge->value==block.first_edge.value)++edge->value;else if(edge->value==block.first_edge.value+1)--edge->value;}branch->predicate=branch->predicate==UnaryOperator::kFalseOrZero?UnaryOperator::kTrueOrNonzeroNumber:UnaryOperator::kFalseOrZero;}
 auto control=Take<ControlPlan>(BuildControlPlan(graph));auto plan=Take<EvaluationPlan>(BuildEvaluationPlan(graph));
 FunctionSyntax syntax{};syntax.identity=identity;syntax.method=input.Method();syntax.owner={0};syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;
 syntax.names={u"o",u"initial",u"a",u"b",u"value",u"effect",u"out"};syntax.parameters={{NameId{0},NameEvidence::kDebugRange}};
 std::vector<ExpressionConstant> constants(graph.ops.size());for(std::size_t i=0;i<graph.ops.size();++i)if(const auto* r=std::get_if<ValueNamedRead>(&graph.ops[i].operation))constants[i]=NameId{r->name.value+1};
 for(std::size_t i=0;i<graph.ops.size();++i)if(const auto* w=std::get_if<ValueNamedWrite>(&graph.ops[i].operation))constants[i]=NameId{w->name.value+1};
 ExpressionConstants resolved{constants,syntax.names.size(),0};
 auto build=[&](SemanticBudget budget){return BuildFunctionTree(graph,control,plan,syntax,resolved,budget);};
 auto built=Take<ConstructedFunction>(build({}));
 auto semantics=Take<TreeSemantics>(VerifyTreeSemantics(graph,control,plan,built.tree,resolved));
 unsigned decisions=0;
 for(std::size_t i=0;i<built.tree.expressions.size();++i){
  const auto* choice=std::get_if<ConditionalExpr>(&built.tree.expressions[i].expression);if(!choice)continue;
  ++decisions;
  if(count==1){
   auto bad=built.tree;auto& changed=std::get<ConditionalExpr>(bad.expressions[i].expression);std::swap(changed.consequent,changed.alternate);
   Check(!std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph,control,plan,bad,resolved)));
   const auto arm=std::holds_alternative<SequenceExpr>(built.tree.expressions[choice->consequent.value].expression)?choice->consequent:choice->alternate;
   const auto comma=std::get<SequenceExpr>(built.tree.expressions[arm.value].expression);
   // Preserve a well-formed owned AST while removing the write itself.
   const auto assignment=std::get<AssignmentExpr>(built.tree.expressions[comma.left.value].expression);
   bad=built.tree;bad.expressions[comma.left.value].expression=UnaryExpr{TreeUnary::kTypeof,assignment.value};
   Check(!std::holds_alternative<TreeStructureError>(VerifyTreeStructure(bad)));
   Check(!std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph,control,plan,bad,resolved)));
   // Duplicate using fresh nodes, not invalid AST sharing: the edge's exact
   // schedule must reject the extra private assignment even if its value agrees.
   bad=built.tree;
   const ExprId copied_rhs{static_cast<std::uint32_t>(bad.expressions.size())};
   Check(std::holds_alternative<SymbolExpr>(bad.expressions[assignment.value.value].expression));
   bad.expressions.push_back(bad.expressions[assignment.value.value]);
   const ExprId duplicate{static_cast<std::uint32_t>(bad.expressions.size())};
   bad.expressions.push_back({AssignmentExpr{assignment.target,copied_rhs},bad.expressions[comma.left.value].origin});
   const ExprId extra{static_cast<std::uint32_t>(bad.expressions.size())};
   bad.expressions.push_back({SequenceExpr{duplicate,comma.right},bad.expressions[arm.value].origin});
   std::get<SequenceExpr>(bad.expressions[arm.value].expression).right=extra;
   Check(!std::holds_alternative<TreeStructureError>(VerifyTreeStructure(bad)));
   Check(!std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph,control,plan,bad,resolved)));
   // Hoist the copy before the predicate, preserving the original expressions
   // but changing their execution edge. Fresh control/ownership must reject it.
   bad=built.tree;
   bad.expressions[i].expression=SequenceExpr{comma.left,arm};
   bad.expressions[arm.value].expression=ConditionalExpr{choice->condition,
       choice->consequent==arm?comma.right:choice->consequent,
       choice->alternate==arm?comma.right:choice->alternate};
   Check(!std::holds_alternative<TreeStructureError>(VerifyTreeStructure(bad)));
   Check(!std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph,control,plan,bad,resolved)));
   bad=built.tree;bad.expressions[comma.left.value].origin=built.tree.expressions[choice->condition.value].origin;
   Check(!std::holds_alternative<TreeSemantics>(VerifyTreeSemantics(graph,control,plan,bad,resolved)));
  }
 }
 if(decisions!=count)std::cerr<<"count="<<count<<" reverse="<<reverse<<" copy="<<copies<<" split="<<split<<" decisions="<<decisions<<"\n";
 Check(decisions==count);
 if(count==1){std::ostringstream out;Take<PrintedFunction>(PrintFunctionTree(built.tree,out));if(print)std::cout<<out.str();}
 std::size_t low=0,high=SemanticBudget{}.bytes;
 while(low+1<high){const auto mid=low+(high-low)/2;if(std::holds_alternative<ConstructedFunction>(build({mid,SemanticBudget{}.work})))high=mid;else low=mid;}
 Take<ConstructedFunction>(build({high,SemanticBudget{}.work}));Check(std::get<FunctionTreeFailure>(build({high-1,SemanticBudget{}.work})).kind==FunctionTreeError::kResourceLimit);
 Take<ConstructedFunction>(build({SemanticBudget{}.bytes,built.work}));
 Check(std::get<FunctionTreeFailure>(build({SemanticBudget{}.bytes,built.work-1})).kind==FunctionTreeError::kWorkLimit);
 return {built.work,high};
}
void CycleConsumer() {
  // The same expression-edge consumer must accept the scheduler's cycle save,
  // not silently assume every bundle is a single assignment. The scheduler's
  // independent symbolic verifier proves the simultaneous swap itself.
  ValueProgram graph; graph.values.resize(2); graph.edges.resize(1);
  EvaluationPlan plan; plan.phis.emplace();
  auto& phis=*plan.phis;
  phis.copies={ParallelCopy{ValueId{0},ValueId{1}},ParallelCopy{ValueId{1},ValueId{0}}};
  const auto schedule=Take<EdgeCopies>(BuildEdgeCopies(phis.copies));
  Check(schedule.temporaries==1 && schedule.steps.size()==3);
  Take<std::uint64_t>(VerifyEdgeCopies(phis.copies,schedule));
  phis.bundles.push_back({EdgeId{0},0,2,schedule,{}});
  FunctionTree tree; tree.body_scope={0};tree.origins.push_back(EdgeOrigin{EdgeId{0}});
  for(unsigned i=0;i<3;++i)tree.symbols.push_back({ScopeId{0},NameId{i},BindingKind::kCompilerTemporary,
      DeclarationKind::kLet,NameEvidence::kSynthesized,i==2?TemporaryReason::kParallelCopyCycle:TemporaryReason::kPhiJoin});
  std::vector<ExprId> assignments;
  for(const auto& step:schedule.steps){
    const auto target=std::visit([](const auto& id){return SymbolId{std::is_same_v<std::decay_t<decltype(id)>,CopyTempId>?2:id.value};},step.destination);
    SymbolId source{};
    if(const auto* value=std::get_if<ValueOperand>(&step.source))source={std::get<ValueId>(*value).value};else source={2};
    const ExprId read{static_cast<std::uint32_t>(tree.expressions.size())};tree.expressions.push_back({SymbolExpr{source},TreeOriginId{0}});
    const ExprId write{static_cast<std::uint32_t>(tree.expressions.size())};tree.expressions.push_back({AssignmentExpr{SymbolTarget{target},read},TreeOriginId{0}});assignments.push_back(write);
  }
  ExprId tail{static_cast<std::uint32_t>(tree.expressions.size())};tree.expressions.push_back({TreeLiteral{BooleanLiteral{false}},TreeOriginId{0}});
  for(auto i=assignments.size();i;--i){const ExprId next{static_cast<std::uint32_t>(tree.expressions.size())};tree.expressions.push_back({SequenceExpr{assignments[i-1],tail},TreeOriginId{0}});tail=next;}
  const auto check=[&](const FunctionTree& input){
    TreeCheckBudget budget({});CompletionTreeView sequences(input,CompletionPlan{},budget);
    TreeFlow flow;flow.definitions.resize(input.expressions.size());std::vector<std::uint8_t> declared(3,1),assigned(3);
    TreeEdgeFlow edges(graph,plan,input,flow,declared,assigned,budget,sequences);
    edges.Decision(EdgeId{0},tail,false);
    Check(flow.copies[0].expressions==assignments && flow.copies[0].temporaries[0]==SymbolId{2});
    Check(flow.phis[0]==SymbolId{0} && flow.phis[1]==SymbolId{1});
    bool duplicate=false;try{edges.Decision(EdgeId{0},tail,false);}catch(TreeFlowError e){duplicate=e==TreeFlowError::kSequence;}Check(duplicate);
  };
  check(tree);
  auto forged=tree;
  std::get<SequenceExpr>(forged.expressions[tail.value].expression).left=assignments[1];
  bool refused=false;try{check(forged);}catch(TreeFlowError){refused=true;}Check(refused);
}

}
int main(int argc,char**){CycleConsumer();if(argc>1){Run(1,false,false,false,true);return 0;}for(bool r:{false,true})for(bool c:{false,true})for(bool s:{false,true})Run(1,r,c,s);std::pair<std::uint64_t,std::size_t> previous{};for(unsigned n:{200,201,400,800}){auto cost=Run(n);if(previous.first)Check(cost.first<3*previous.first&&cost.second<3*previous.second);previous=cost;std::cout<<n<<" work="<<cost.first<<" bytes="<<cost.second<<'\n';}}
