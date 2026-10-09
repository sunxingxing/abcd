#include "../../../src/source/recovery_projection.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <source_location>
using namespace abcd;
using namespace abcd::source;
namespace {
void Check(bool value,std::source_location at=std::source_location::current()) {
  if(!value){std::cerr<<"projection check "<<at.line()<<'\n';std::abort();}
}
struct Fixture {
  FunctionTree tree{};
  BindingInitializers plan;
  OriginalFlow original{};
  std::vector<FlowValueEquation> equations;
  std::vector<ExprId> order;
  std::vector<std::uint8_t> private_bindings;
  std::vector<ExprId> reads,values,stores,results;
  std::vector<StmtId> declarations;
  Fixture(unsigned count,bool reverse) {
    tree.callable=CallableKind::kFunction;tree.strictness=Strictness::kStrict;
    tree.parameter_scope={0};tree.body_scope={1};tree.body={0};
    tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
    tree.origins={OriginalSite{{0},{0}}};tree.names={u"input"};
    tree.symbols.push_back({{0},{0},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}});
    tree.parameters.push_back({{0},{},false});tree.statements.push_back({BlockStmt{{1},0,0},{0}});
    plan.primitives={UndefinedLiteral{},IntegerLiteral{7}};
    const auto expr=[&](TreeExpression node,FlowValueEquation equation=UnknownFlowValue{}) {
      ExprId id{static_cast<unsigned>(tree.expressions.size())};tree.expressions.push_back({std::move(node),{0}});
      equations.push_back(equation);order.push_back(id);return id;
    };
    const auto stmt=[&](TreeStatement node) {
      StmtId id{static_cast<unsigned>(tree.statements.size())};tree.statements.push_back({std::move(node),{0}});tree.children.push_back(id);return id;
    };
    for(unsigned i=0;i<count;++i) {
      SymbolId binding{static_cast<unsigned>(tree.symbols.size())};NameId name{static_cast<unsigned>(tree.names.size())};
      tree.names.push_back(u"saved");tree.symbols.push_back({{1},name,BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
      declarations.push_back(stmt(DeclarationStmt{binding,expr(TreeLiteral{IntegerLiteral{7}},FlowPrimitiveReference{{1}})}));
      const auto callee=expr(SymbolExpr{{0}},FlowBindingValue{{0}});
      values.push_back(expr(CallExpr{callee,{0,0},CallReference::kValue}));
      stores.push_back(expr(AssignmentExpr{SymbolTarget{binding},values.back()},FlowExpressionValue{values.back()}));
      stmt(ExpressionStmt{stores.back()});
      reads.push_back(expr(SymbolExpr{binding},FlowBindingValue{binding}));
      results.push_back(expr(UnaryExpr{TreeUnary::kNegate,reads.back()}));
      stmt(ExpressionStmt{results.back()});
    }
    std::get<BlockStmt>(tree.statements[0].statement).child_count=tree.children.size();
    const auto bindings=tree.symbols.size(),expressions=tree.expressions.size(),statements=tree.statements.size();
    plan.aliases.resize(bindings);plan.storage.resize(bindings);plan.read_bindings.resize(bindings);plan.literals.resize(bindings);
    plan.read_sources.resize(expressions);plan.read_literals.resize(expressions);plan.replacements.resize(expressions);plan.transports.resize(expressions);
    plan.omitted.resize(statements);plan.statements.resize(statements);private_bindings.resize(bindings,1);private_bindings[0]=0;
    original.identity=tree.identity;original.method=tree.method;original.value_revision=tree.value_revision;original.cfg_revision=tree.cfg_revision;
    original.statements=statements;original.bindings=bindings;original.normal_kinds.resize(expressions);original.input_ranges.resize(expressions);
    // Synthetic analysis input: a trial is allowed to ask about an unproved
    // replacement. Its old occurrence's type is not proof for the new value.
    for(auto read:reads)original.normal_kinds[read.value]=analysis::ValueKinds::Number();
    if(reverse) {
      const auto map=[&](ExprId id){return ExprId{static_cast<unsigned>(expressions-1-id.value)};};
      for(auto& item:tree.expressions)std::visit([&](auto& node){using T=std::decay_t<decltype(node)>;
        if constexpr(std::is_same_v<T,CallExpr>)node.callee=map(node.callee);
        else if constexpr(std::is_same_v<T,AssignmentExpr>)node.value=map(node.value);
        else if constexpr(std::is_same_v<T,UnaryExpr>)node.operand=map(node.operand);
      },item.expression);
      for(auto& item:tree.statements)std::visit([&](auto& node){using T=std::decay_t<decltype(node)>;
        if constexpr(std::is_same_v<T,DeclarationStmt>){if(node.initializer)node.initializer=map(*node.initializer);}
        else if constexpr(std::is_same_v<T,ExpressionStmt>)node.expression=map(node.expression);
      },item.statement);
      for(auto& equation:equations)if(auto* value=std::get_if<FlowExpressionValue>(&equation))value->expression=map(value->expression);
      for(auto* ids:{&reads,&values,&stores,&results,&order})for(auto& id:*ids)id=map(id);
      std::reverse(tree.expressions.begin(),tree.expressions.end());std::reverse(equations.begin(),equations.end());
      std::reverse(original.normal_kinds.begin(),original.normal_kinds.end());
    }
  }
  RecoveryExpressionInput Input() const {return {plan,equations,order,plan.replacements,plan.transports,&original};}
  LegacyFlowProjection Projection() const {
    LegacyFlowProjection result{plan.aliases,plan.literals,plan.replacements,plan.omitted,{},{},plan.statements,private_bindings};
    result.storage=plan.storage;result.read_literals=plan.read_literals;result.transports=plan.transports;
    result.read_bindings=plan.read_bindings;result.read_sources=plan.read_sources;result.original=&original;return result;
  }
};
void Case(bool reverse) {
  Fixture f(1,reverse);
  const auto current=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input()));
  Check(current.initialized_reads[f.reads[0].value]);
  Check(current.expressions.evaluations[f.results[0].value]==ExpressionEvaluation::kInert);
  f.plan.replacements[f.reads[0].value]=f.values[0];
  f.plan.transports[f.stores[0].value]=ExpressionTransport{f.values[0],PrimitiveId{0}};
  const auto trial=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input()));
  std::reverse(f.order.begin(),f.order.end());
  const auto reordered=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input()));
  Check(reordered.expressions.evaluations==trial.expressions.evaluations);
  Check(reordered.expressions.kinds==trial.expressions.kinds);
  std::reverse(f.order.begin(),f.order.end());
  Check(trial.expressions.kinds[f.reads[0].value]==analysis::ValueKinds::Language());
  Check(trial.flow.normal_kinds[f.reads[0].value]==analysis::ValueKinds::Language());
  Check(trial.expressions.evaluations[f.results[0].value]==ExpressionEvaluation::kRequired);
  Check(trial.expressions.evaluations[f.stores[0].value]==ExpressionEvaluation::kInert);
  Check(trial.expressions.bindings[f.stores[0].value]==std::nullopt);
  const auto retained=trial.flow.events.capacity()*sizeof(FlowEvent)+
      trial.flow.edges.capacity()*sizeof(FlowEdge)+trial.flow.definitions.capacity()*sizeof(FlowDefinition)+
      trial.flow.references.capacity()*sizeof(FlowReference)+
      trial.expressions.bindings.capacity()*sizeof(std::optional<SymbolId>)+
      trial.expressions.literals.capacity()*sizeof(std::optional<PrimitiveId>)+
      trial.expressions.roots.capacity()*sizeof(ExprId)+trial.expressions.kinds.capacity()*sizeof(analysis::ValueKinds)+
      trial.initialized_reads.capacity()*sizeof(std::uint8_t);
  Check(trial.allocated_bytes>=retained);
  Check(std::get<TreeStructureError>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input(),{retained-1,trial.work}))==TreeStructureError::kResourceLimit);
  Check(trial.flow.normal_exits[f.values[0].value].has_value());
  Check(trial.flow.normal_exits[f.stores[0].value].has_value());
  Check(current.expressions.evaluations[f.results[0].value]==ExpressionEvaluation::kInert);
  f.plan.replacements[f.reads[0].value].reset();f.plan.transports[f.stores[0].value].reset();
  f.plan.omitted[f.declarations[0].value]=1;
  const auto uninitialized=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input()));
  Check(!uninitialized.initialized_reads[f.reads[0].value]);
  Check(uninitialized.expressions.evaluations[f.reads[0].value]==ExpressionEvaluation::kRequired);
  // Supplying another projection's bootstrap proof is not a trial API option.
  // Exact budgets include both provisional/refined facts and shared analyses.
  const analysis::SemanticBudget exact{trial.allocated_bytes,trial.work};
  f.plan.omitted[f.declarations[0].value]=0;
  f.plan.replacements[f.reads[0].value]=f.values[0];f.plan.transports[f.stores[0].value]=ExpressionTransport{f.values[0],PrimitiveId{0}};
  Check(std::holds_alternative<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input(),exact)));
  Check(std::get<TreeStructureError>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input(),{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input(),{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
  f.plan.replacements[f.reads[0].value]=f.reads[0];
  Check(std::get<TreeStructureError>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input()))==TreeStructureError::kOwnership);
  f.plan.replacements[f.reads[0].value].reset();f.plan.transports[f.stores[0].value].reset();
  auto raw=f.Input();raw.original=nullptr;
  const auto conservative=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),raw));
  Check(conservative.expressions.kinds.empty()&&conservative.flow.normal_kinds.empty());
  Check(conservative.expressions.evaluations[f.results[0].value]==ExpressionEvaluation::kRequired);
}
}
int main() {
  {
    TreeCheckBudget budget({16,100});budget.Admit(12,1);budget.Step(7);
    budget.Release(12);Check(budget.Bytes()==12&&budget.ActiveBytes()==0&&budget.Work()==7);
    budget.Admit(16,1);Check(budget.Bytes()==16&&budget.ActiveBytes()==16);
    budget.Release(16);Check(budget.Bytes()==16&&budget.Work()==7);
    bool rejected=false;try{budget.Release(1);}catch(TreeStructureError error){rejected=error==TreeStructureError::kOwnership;}
    Check(rejected);rejected=false;
    try{budget.Admit(17,1);}catch(TreeStructureError error){rejected=error==TreeStructureError::kResourceLimit;}Check(rejected);
    rejected=false;try{budget.Step(94);}catch(TreeStructureError error){rejected=error==TreeStructureError::kWorkLimit;}Check(rejected);
  }
  Case(false);Case(true);
  std::uint64_t work=0;std::size_t bytes=0;
  for(auto count:{200u,201u,400u,800u}) {
    Fixture f(count,true);const auto result=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input()));
    if(work){Check(result.work<work*3);Check(result.allocated_bytes<bytes*3);}work=result.work;bytes=result.allocated_bytes;
    std::cout<<count<<" expressions="<<f.tree.expressions.size()<<" events="<<result.flow.events.size()<<" work="<<work<<" bytes="<<bytes<<'\n';
  }
}
