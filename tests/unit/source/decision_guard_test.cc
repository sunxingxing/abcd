#include "analysis/guarded_fixture.h"
#include "../../../src/source/evaluation_disposition.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/tree_flow.h"
#include "../../../src/source/tree_checks.h"

using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
template<class T, class V> T Take(V result, std::source_location at=std::source_location::current()) {
  if(!std::holds_alternative<T>(result)) {
    std::cerr<<"decision_guard_test:"<<at.line()<<" variant="<<result.index()<<'\n';
    std::visit([](const auto& e){if constexpr(std::is_enum_v<std::decay_t<decltype(e)>>)std::cerr<<unsigned(e)<<'\n';else if constexpr(requires{e.kind;})std::cerr<<unsigned(e.kind)<<'\n';},result);
    std::abort();
  }
  return std::get<T>(std::move(result));
}
abc::MethodBody Body(std::size_t count, bool copies, bool split) {
  std::vector<std::uint8_t> bytes;
  const auto op = [&](abc::Opcode code) {
    const auto n = static_cast<std::uint16_t>(code); bytes.push_back(n);
    if (n > UINT8_MAX) bytes.push_back(n >> 8);
  };
  const auto jump = [&](abc::Opcode code) {
    auto at = bytes.size(); op(code); bytes.push_back(0); return at;
  };
  const auto patch = [&](std::size_t at) { test::Check(bytes.size() - at < 128); bytes[at+1] = bytes.size()-at; };
  for (std::size_t i=0;i<count;++i) {
    std::vector<std::size_t> joins;
    for (unsigned term=0;term<2;++term) {
      op(abc::Opcode::kLDA_V8); bytes.push_back(2);
      op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); bytes.insert(bytes.end(),{0,static_cast<std::uint8_t>(term),0});
      op(abc::Opcode::kSTA_V8); bytes.push_back(0);
      op(abc::Opcode::kLDLEXVAR_IMM4_IMM4);bytes.push_back(0);
      if(copies){op(abc::Opcode::kSTA_V8);bytes.push_back(1);op(abc::Opcode::kLDA_V8);bytes.push_back(1);}
      if(split){op(abc::Opcode::kJMP_IMM8);bytes.push_back(2);}
      op(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16);bytes.insert(bytes.end(),{0,0});
      op(abc::Opcode::kSTRICTNOTEQ_IMM8_V8);bytes.insert(bytes.end(),{0,0});
      joins.push_back(jump(abc::Opcode::kJEQZ_IMM8));
    }
    const auto skip=jump(abc::Opcode::kJMP_IMM8);
    for(auto at:joins)patch(at);
    op(abc::Opcode::kLDA_V8);bytes.push_back(2);
    op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16);bytes.insert(bytes.end(),{0,2,0});
    patch(skip);
    op(abc::Opcode::kLDA_V8);bytes.push_back(2);
    op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16);bytes.insert(bytes.end(),{0,3,0});
  }
  const auto end=static_cast<std::uint32_t>(bytes.size());op(abc::Opcode::kRETURNUNDEFINED);
  op(abc::Opcode::kRETURN);
  auto body=test::Body(bytes);
  abc::TryRegion region;region.offset={0};region.length=end;region.handler_count=1;
  abc::CatchHandler handler;handler.offset={end+1};handler.length=1;
  body.tries.push_back(region);body.handlers.push_back(handler);return body;
}
auto Run(std::size_t count,bool copies=false,bool split=false,bool checked=false) {
  const auto body=Body(count,copies,split);
  const auto frame=Take<abc::FrameLayout>(abc::BuildFrameLayout(2,1,{}));
  const auto flow=Take<ExceptionFlow>(BuildExceptionFlow(body,abc::ProfileId::kDynamic1301,frame.registers));
  IdfQuota quota{kDefaultSemanticWork};const auto ssa=Take<ExceptionSsa>(ExceptionSsa::Build(flow,frame.registers,quota));
  const auto environments=Take<EnvironmentFlow>(AnalyzeExceptionEnvironments(flow));
  const auto captures=Take<ExceptionCaptures>(PlanExceptionCaptures(flow,environments));
  std::vector<EntryValueFact> entries;
  for(std::size_t i=0;i<ssa.Values().size();++i) {
    const auto& value=ssa.Values()[i];const auto* reg=std::get_if<abc::RegisterId>(&value.slot);
    if(std::holds_alternative<MethodEntry>(value.origin)&&reg&&reg->value>=frame.locals)
      entries.push_back({{static_cast<std::uint32_t>(i)},ValueKinds::Language()});
  }
  IdfQuota value_work{kDefaultSemanticWork};
  const auto values=Take<ExceptionValueFlow>(ExceptionValueFlow::Build(flow,ssa,entries,value_work,{},PendingExceptionValues::kLanguage,captures.loads,{},CallResultValues::kLanguage));
  const auto proof=Take<ExceptionInputProof>(VerifyExceptionInputs(flow,ssa,environments,values));
  const PhasedValueInput input{flow,ssa,environments,values,proof,frame};
  const auto graph=Take<ValueProgram>(ImportPhasedValues(input));
  const auto pairs=Take<GuardedReads>(ProveGuardedReads(graph));
  test::Check(pairs.reads.size()==2*count);
  const auto plan=Take<EvaluationPlan>(BuildPhasedEvaluationPlan(graph,{},IncomingCells::kConditional,{}, {},{}, {},ClosureWrites::kLanguage,{},checked?std::span<const GuardedRead>(pairs.reads):std::span<const GuardedRead>{}));
  for(const auto& pair:pairs.reads)test::Check(plan.operations[pair.guard.value].kind==(checked?EvaluationKind::kGuardedCellRead:EvaluationKind::kCellGuard));
  const auto regions=Take<ExceptionPlan>(BuildExceptionPlan(body,graph,input));
  const auto control=Take<ExceptionControl>(BuildExceptionControl(body,graph,input,regions));
  std::size_t decisions=0;for(const auto& node:control.skeleton.nodes)decisions+=node.decision.has_value();
  test::Check(decisions!=0);
  const ExceptionTreeInput exceptions{body,input,regions,control};
  FunctionSyntax syntax{};syntax.owner={2};syntax.callable=CallableKind::kFunction;syntax.strictness=Strictness::kStrict;
  syntax.names={u"captured",u"target",u"left",u"right",u"effect",u"tail"};
  syntax.parameters={{NameId{1},NameEvidence::kDebugRange}};
  syntax.captures.push_back({CellId{0},{{1},{0}},{0}});
  std::vector<ExpressionConstant> constants(graph.ops.size());
  for(std::size_t i=0;i<graph.ops.size();++i)
    if(const auto* read=std::get_if<ValueNamedRead>(&graph.ops[i].operation))constants[i]=NameId{read->name.value+2};
  const ExpressionConstants resolved{constants,syntax.names.size()};
  const auto build=[&](SemanticBudget budget=SemanticBudget{}){return BuildExceptionFunctionDraft(graph,exceptions,plan,syntax,resolved,budget);};
  const auto built=build();
  if(checked){
    // A real throwing TDZ event is not an initialized anchor. In particular,
    // shared decision ownership cannot erase or duplicate that checked read.
    test::Check(std::holds_alternative<FunctionTreeFailure>(built));
    const auto& failure=std::get<FunctionTreeFailure>(built);
    test::Check(failure.kind==FunctionTreeError::kOwnership && failure.tree &&
        failure.tree->placement==EvaluationKind::kGuardedCellRead &&
        failure.tree->ownership==ExpressionOwnershipError::kPlacement);
    auto forged=plan;for(const auto& pair:pairs.reads)forged.operations[pair.guard.value].kind=EvaluationKind::kCellGuard;
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph,forged)));
    return std::pair<std::uint64_t,std::size_t>{0,0};
  }
  if(const auto* failure=std::get_if<FunctionTreeFailure>(&built))std::cerr<<"tree "<<unsigned(failure->kind)<<" count "<<count<<'\n';
  test::Check(std::holds_alternative<ExceptionFunctionDraft>(built));
  const auto& draft=std::get<ExceptionFunctionDraft>(built);
  test::Check(std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph,exceptions,plan,draft.tree,resolved)));
  std::size_t anchors=0;
  for(const auto& pair:pairs.reads){
    const auto p=std::find_if(draft.tree.operations.begin(),draft.tree.operations.end(),[&](const auto& p){return p.operation==pair.guard;});
    test::Check(p!=draft.tree.operations.end()&&std::holds_alternative<AnchorId>(p->node));++anchors;
  }
  test::Check(anchors==2*count);
  if(count==1){
    const auto& pair=pairs.reads.back();
    const auto reject=[&](const FunctionTree& t){test::Check(!std::holds_alternative<TreeSemantics>(VerifyExceptionTreeSemantics(graph,exceptions,plan,t,resolved)));};
    auto changed=draft.tree;auto p=std::find_if(changed.operations.begin(),changed.operations.end(),[&](const auto& p){return p.operation==pair.guard;});
    changed.operations.erase(p);reject(changed);
    changed=draft.tree;changed.operations.push_back(*std::find_if(changed.operations.begin(),changed.operations.end(),[&](const auto& p){return p.operation==pair.guard;}));reject(changed);
    changed=draft.tree;p=std::find_if(changed.operations.begin(),changed.operations.end(),[&](const auto& p){return p.operation==pair.guard;});
    p->node=ExprId{0};reject(changed);
    changed=draft.tree;p=std::find_if(changed.operations.begin(),changed.operations.end(),[&](const auto& p){return p.operation==pair.guard;});
    const auto anchor=std::get<AnchorId>(p->node);
    auto* boundary=std::get_if<BlockBoundary>(&changed.anchors[anchor.value].position);
    test::Check(boundary);++boundary->index;reject(changed);
    auto forged=plan;forged.cells->incoming=IncomingCells::kReject;
    test::Check(std::holds_alternative<EvaluationFailure>(VerifyPhasedEvaluationPlan(graph,forged)));
  }
  test::Check(std::holds_alternative<ExceptionFunctionDraft>(build({SemanticBudget{}.bytes,draft.work})));
  test::Check(std::get<FunctionTreeFailure>(build({SemanticBudget{}.bytes,draft.work-1}))==FunctionTreeError::kWorkLimit);
  std::size_t low=0,high=SemanticBudget{}.bytes;
  while(low+1<high){const auto mid=low+(high-low)/2;if(std::holds_alternative<ExceptionFunctionDraft>(build({mid,SemanticBudget{}.work})))high=mid;else low=mid;}
  test::Check(std::holds_alternative<ExceptionFunctionDraft>(build({high,SemanticBudget{}.work})));
  test::Check(std::get<FunctionTreeFailure>(build({high-1,SemanticBudget{}.work}))==FunctionTreeError::kResourceLimit);
  return std::pair{draft.work,high};
}
}
int main(){
  static_assert(Disposition(EvaluationKind::kCellGuard)==EvaluationDisposition::kAnchor);
  static_assert(Disposition(EvaluationKind::kGuardedCellRead)==EvaluationDisposition::kCheckedRead);
  std::pair<std::uint64_t,std::size_t> previous{};
  for(auto count:{1U,200U,201U,400U,800U}){const auto cost=Run(count);if(count>200)test::Check(cost.first<=3*previous.first&&cost.second<=3*previous.second);previous=cost;}
  Run(1,true);Run(1,false,true);Run(1,true,true);Run(1,false,false,true);
}
