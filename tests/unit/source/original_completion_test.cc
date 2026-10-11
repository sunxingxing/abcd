#include "../../../src/source/function_tree.h"
#include "../../../src/source/exception_tree.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/source_variables.h"
#include "../../../src/source/recovery_projection.h"
#include "../../../src/source/logical_regions.h"
#include "../../../src/analysis/phased_value_import.h"
#include "abcd/analysis/value_import.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <source_location>
using namespace abcd;
using namespace abcd::source;
using namespace abcd::analysis;
namespace {
void Check(bool value,std::source_location at=std::source_location::current()) {
  if(!value){std::cerr<<"original completion check "<<at.line()<<'\n';std::abort();}
}
template<class T,class V> decltype(auto) Take(V&& value,std::source_location at=std::source_location::current()) {
  if(!std::holds_alternative<T>(value)) {
    std::cerr<<"original completion variant "<<at.line()<<" alternative="<<value.index()<<'\n';
    std::visit([](const auto& error){if constexpr(std::is_enum_v<std::decay_t<decltype(error)>>)std::cerr<<"error="<<static_cast<unsigned>(error)<<'\n';},value);
    std::abort();
  }
  return std::get<T>(std::forward<V>(value));
}
void Reverse(FunctionTree& tree) {
  const auto expr=[&](ExprId id){return ExprId{static_cast<unsigned>(tree.expressions.size()-1-id.value)};};
  const auto stmt=[&](StmtId id){return StmtId{static_cast<unsigned>(tree.statements.size()-1-id.value)};};
  for(auto& item:tree.expressions)std::visit([&](auto& node) {
    using T=std::decay_t<decltype(node)>;
    if constexpr(std::is_same_v<T,UnaryExpr>)node.operand=expr(node.operand);
    else if constexpr(std::is_same_v<T,BinaryExpr>||std::is_same_v<T,LogicalExpr>||std::is_same_v<T,SequenceExpr>) {
      node.left=expr(node.left);node.right=expr(node.right);
    } else if constexpr(std::is_same_v<T,ConditionalExpr>) {
      node.condition=expr(node.condition);node.consequent=expr(node.consequent);node.alternate=expr(node.alternate);
    } else if constexpr(std::is_same_v<T,AssignmentExpr>)node.value=expr(node.value);
    else if constexpr(std::is_same_v<T,CallExpr>)node.callee=expr(node.callee);
    else Check((std::is_same_v<T,SymbolExpr>||std::is_same_v<T,TreeLiteral>||std::is_same_v<T,EmptyArrayExpr>));
  },item.expression);
  for(auto& item:tree.statements)std::visit([&](auto& node) {
    using T=std::decay_t<decltype(node)>;
    if constexpr(std::is_same_v<T,ReturnStmt>){if(node.value)node.value=expr(*node.value);}
    else if constexpr(std::is_same_v<T,ExpressionStmt>)node.expression=expr(node.expression);
    else if constexpr(std::is_same_v<T,DeclarationStmt>){if(node.initializer)node.initializer=expr(*node.initializer);}
    else if constexpr(std::is_same_v<T,IfStmt>){node.condition=expr(node.condition);node.consequent=stmt(node.consequent);if(node.alternate)node.alternate=stmt(*node.alternate);}
    else Check((std::is_same_v<T,BlockStmt>||std::is_same_v<T,DebuggerStmt>));
  },item.statement);
  for(auto& item:tree.operations)std::visit([&](auto& node) {
    using T=std::decay_t<decltype(node)>;
    if constexpr(std::is_same_v<T,ExprId>)node=expr(node);
    else if constexpr(std::is_same_v<T,StmtId>)node=stmt(node);
  },item.node);
  for(auto& argument:tree.arguments)argument=expr(argument);
  for(auto& child:tree.children)child=stmt(child);
  for(auto& anchor:tree.anchors)if(auto* boundary=std::get_if<BlockBoundary>(&anchor.position))boundary->block=stmt(boundary->block);
  tree.body=stmt(tree.body);
  std::reverse(tree.statements.begin(),tree.statements.end());std::reverse(tree.expressions.begin(),tree.expressions.end());
}
struct Case {abc::ProfileId profile;bool phased,positive,reverse,split,reallocated;unsigned copies;};
struct Cost {std::uint64_t work;std::size_t bytes;};
Cost CheckSource(const Case& c,const ValueProgram& graph,const FunctionTree& tree,const OriginalFlow& original,bool emit) {
  const auto variables=Take<SourceVariables>(PrepareSourceVariables(tree,{},&original));const auto& plan=variables.bindings;
  LegacyFlowProjection p{plan.aliases,plan.literals,plan.replacements,plan.omitted,{}, {},plan.statements,{},
      plan.storage,plan.undefined_stores,plan.declaration_values,plan.declaration_completions};
  std::vector<FlowValueEquation> equations(tree.expressions.size());std::vector<ExprId> order;
  for(unsigned i=0;i<tree.expressions.size();++i){order.push_back({i});if(const auto* read=std::get_if<SymbolExpr>(&tree.expressions[i].expression))equations[i]=FlowBindingValue{read->symbol};}
  const RecoveryExpressionInput input{plan,equations,order,&original};
  const auto model=Take<RecoveryProjection>(PrepareRecoveryProjection(tree,p,input));
  unsigned decisions=0,statements=0;
  for(unsigned i=0;i<graph.ops.size();++i) {
    const auto& operation=graph.ops[i];
    if(std::holds_alternative<ValueBranch>(operation.operation)) {
      ++decisions;const auto& control=original.operations[i].control;Check(control&&control->test);
      const auto proof=PureBranchTruth(graph,{i});Check(proof.has_value());
      // The conditional keeps its input value; the common adapter keeps raw
      // edge polarity independently of whichever source arm prints first.
      Check(proof->true_values==(c.positive?TruthDomains::Truthy():TruthDomains::Truthy().Complement()));
      const auto occurrence=FindFlowOriginalControl(model.flow,{i});Check(occurrence.has_value());
      if(c.phased)for(unsigned j=0;j<original.phases.size();++j) {
        const auto& phase=original.phases[j];if(phase.operation!=OpId{i}||phase.phase==ExceptionPhase::kCatch)continue;
        Check(FindOriginalControlPhase(model.flow,{j},phase.phase,{i}).has_value());
        Check(!FindOriginalPhase(model.flow,{j},phase.phase,OpId{i}));
      }
      auto changed=graph;const auto operand=changed.uses[operation.first_use.value].operand;
      const auto value=Take<ValueId>(operand);changed.values[value.value].domain=ValueKinds::Language();
      const auto producer=Take<OpResult>(changed.values[value.value].origin).op;
      changed.ops[producer.value].operation=ValueInvoke{};
      Check(!PureBranchTruth(changed,{i})); // Unknown input cannot use Boolean-only numeric truth.
    }
    if(!std::holds_alternative<ValueDebugger>(operation.operation))continue;
    ++statements;Check(original.operations[i].statement_kind==OriginalStatementKind::kNormal);
    const auto site=*original.operations[i].statement;
    if(!c.phased)continue;
    const auto& action=model.flow.original_actions[i];Check(action&&action->normal);
    Check(action->before==model.flow.statements[site.value]);
    Check(model.flow.events[action->before.value].effect==ExpressionEvaluation::kRequired);
    bool normal=false;for(auto edge=model.flow.events[action->before.value].outgoing;edge;edge=model.flow.edges[*edge].outgoing)
      normal|=model.flow.edges[*edge].completion==FlowCompletion::kNormal&&model.flow.edges[*edge].to==*action->normal;
    Check(normal);
    for(unsigned j=0;j<original.phases.size();++j) {
      const auto& phase=original.phases[j];if(phase.operation!=OpId{i})continue;
      Check(FindOriginalPhase(model.flow,{j},phase.phase,OpId{i})==(phase.phase==ExceptionPhase::kBefore?std::optional{action->before}:action->normal));
    }
    auto candidate=plan;candidate.omitted[site.value]=1;p.omitted=candidate.omitted;
    const auto trial=Take<RecoveryProjection>(PrepareRecoveryProjection(tree,p,{candidate,equations,order,&original}));
    Check(!trial.flow.original_actions[i]);
    for(unsigned j=0;j<original.phases.size();++j)if(original.phases[j].operation==OpId{i})
      Check(!FindOriginalPhase(trial.flow,{j},original.phases[j].phase,OpId{i}));
    p.omitted=plan.omitted;
  }
  Check(decisions==1&&statements==1);
  std::ostringstream source;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,source,{}, {},nullptr,&original)));
  if(emit)std::cout<<nlohmann::json{{"source",source.str()},{"phased",c.phased},{"positive",c.positive},
      {"reverse",c.reverse},{"split",c.split},{"reallocated",c.reallocated},{"copies",c.copies},
      {"profile",static_cast<unsigned>(c.profile)},{"work",model.work},{"bytes",model.allocated_bytes}}<<'\n';
  if(c.copies==1) {
    const SemanticBudget exact{model.allocated_bytes,model.work};
    Check(std::holds_alternative<RecoveryProjection>(PrepareRecoveryProjection(tree,p,input,exact)));
    Check(Take<TreeStructureError>(PrepareRecoveryProjection(tree,p,input,{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
    Check(Take<TreeStructureError>(PrepareRecoveryProjection(tree,p,input,{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
  }
  return {model.work,model.allocated_bytes};
}
Cost Run(const Case& c,bool emit) {
  const auto op=[](abc::Opcode code){return static_cast<std::uint8_t>(code);};
  const std::uint8_t scratch=c.reallocated?2:0,locals=scratch+1;
  std::vector<std::uint8_t> bytes{op(abc::Opcode::kDEBUGGER),op(abc::Opcode::kLDAI_IMM32),0,0,0,0,
      op(abc::Opcode::kSTA_V8),scratch,op(abc::Opcode::kLDA_V8),locals,op(abc::Opcode::kCALLARG0_IMM8),0};
  // Copy an arbitrary callback result without replacing the saved comparison operand.
  for(unsigned i=0;i<c.copies;++i)bytes.insert(bytes.end(),{op(abc::Opcode::kSTA_V8),static_cast<std::uint8_t>(locals+1),op(abc::Opcode::kLDA_V8),static_cast<std::uint8_t>(locals+1)});
  bytes.insert(bytes.end(),{op(abc::Opcode::kSTRICTEQ_IMM8_V8),0,scratch});
  if(c.split)bytes.insert(bytes.end(),{op(abc::Opcode::kJMP_IMM8),2});
  const auto branch=bytes.size();bytes.insert(bytes.end(),{op(c.positive?abc::Opcode::kJNEZ_IMM8:abc::Opcode::kJEQZ_IMM8),0});
  bytes.push_back(op(abc::Opcode::kCREATEEMPTYARRAY_IMM8));bytes.push_back(0);
  const auto jump=bytes.size();bytes.insert(bytes.end(),{op(abc::Opcode::kJMP_IMM8),0});
  bytes[branch+1]=static_cast<std::uint8_t>(bytes.size()-branch);
  bytes.insert(bytes.end(),{op(abc::Opcode::kLDAI_IMM32),7,0,0,0});
  bytes[jump+1]=static_cast<std::uint8_t>(bytes.size()-jump);bytes.push_back(op(abc::Opcode::kRETURN));
  const auto body=test::Body(bytes);
  // The callback is parameter 0; copy storage is parameter 1, an unobserved
  // writable invocation slot, so register reallocation retains both identities.
  const auto frame=Take<abc::FrameLayout>(abc::BuildFrameLayout(locals,2,{}));
  FunctionSyntax syntax{};syntax.identity.profile=c.profile;syntax.callable=CallableKind::kFunction;
  syntax.strictness=Strictness::kStrict;syntax.parameters.resize(2);
  if(!c.phased) {
    const auto method=Take<SemanticMethod>(BuildSemanticMethod(body,c.profile,frame.registers));
    const auto proof=Take<SemanticProof>(VerifySemanticSource(body,method,frame.registers,frame.first_parameter.value,{},CapturedValues::kReject,{},ClosureWrites::kUnknown,{},CallResultValues::kLanguage));
    const auto graph=Take<ValueProgram>(ImportNormalValues(body,method,proof,frame));
    const auto control=Take<ControlPlan>(BuildControlPlan(graph));const auto evaluation=Take<EvaluationPlan>(BuildEvaluationPlan(graph));
    std::vector<ExpressionConstant> constants(graph.ops.size());
    auto constructed=Take<ConstructedFunction>(BuildFunctionTree(graph,control,evaluation,syntax,{constants,0}));
    if(c.reverse){Reverse(constructed.tree);constructed.flow=Take<TreeSemantics>(VerifyTreeSemantics(graph,control,evaluation,constructed.tree,{constants,0})).flow;}
    return CheckSource(c,graph,constructed.tree,*constructed.flow->original,emit);
  }
  const auto raw=Take<ExceptionFlow>(BuildExceptionFlow(body,c.profile,frame.registers));
  IdfQuota ssa_work{kDefaultSemanticWork};const auto ssa=Take<ExceptionSsa>(ExceptionSsa::Build(raw,frame.registers,ssa_work));
  const auto env=Take<EnvironmentFlow>(AnalyzeExceptionEnvironments(raw));std::vector<EntryValueFact> entries;
  for(unsigned i=0;i<ssa.Values().size();++i)if(std::holds_alternative<MethodEntry>(ssa.Values()[i].origin))entries.push_back({{i},ValueKinds::Language()});
  IdfQuota value_work{kDefaultSemanticWork};const auto values=Take<ExceptionValueFlow>(ExceptionValueFlow::Build(raw,ssa,entries,value_work,{},PendingExceptionValues::kLanguage,{}, {},CallResultValues::kLanguage));
  const auto proof=Take<ExceptionInputProof>(VerifyExceptionInputs(raw,ssa,env,values));const PhasedValueInput input{raw,ssa,env,values,proof,frame};
  const auto graph=Take<ValueProgram>(ImportPhasedValues(input));const auto evaluation=Take<EvaluationPlan>(BuildPhasedEvaluationPlan(graph));
  const auto regions=Take<ExceptionPlan>(BuildExceptionPlan(body,graph,input));const auto control=Take<ExceptionControl>(BuildExceptionControl(body,graph,input,regions));
  const ExceptionTreeInput context{body,input,regions,control};std::vector<ExpressionConstant> constants(graph.ops.size());
  auto draft=Take<ExceptionFunctionDraft>(BuildExceptionFunctionDraft(graph,context,evaluation,syntax,{constants,0}));
  if(c.reverse){Reverse(draft.tree);draft.flow=Take<TreeSemantics>(VerifyExceptionTreeSemantics(graph,context,evaluation,draft.tree,{constants,0})).flow;}
  return CheckSource(c,graph,draft.tree,*draft.flow->original,emit);
}
}
int main(int argc,char**) {
  for(auto profile:{abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1206,abc::ProfileId::kDynamic1301})
    for(bool phased:{false,true})for(bool positive:{false,true})for(bool reverse:{false,true})for(bool split:{false,true})for(bool reallocated:{false,true})
      for(unsigned copies:{0U,1U})Run({profile,phased,positive,reverse,split,reallocated,copies},argc>1);
  for(bool phased:{false,true})for(bool reverse:{false,true}) {
    std::optional<Cost> previous;
    for(unsigned copies:{200U,201U,400U,800U}) {
      const auto cost=Run({abc::ProfileId::kDynamic1206,phased,false,reverse,false,false,copies},argc>1);
      if(previous){const auto ratio=copies==201?1.1:3.0;Check(cost.work<previous->work*ratio&&cost.bytes<previous->bytes*ratio);}
      previous=cost;
    }
  }
}
