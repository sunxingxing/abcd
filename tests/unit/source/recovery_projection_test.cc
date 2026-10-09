#include "../../../src/source/recovery_projection.h"
#include "../../../src/source/snapshot_projection.h"
#include "../../../src/source/flow_kinds.h"
#include "abcd/source/tree_printer.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>
#include <array>
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
      const auto call=expr(CallExpr{callee,{0,0},CallReference::kValue});
      values.push_back(expr(UnaryExpr{TreeUnary::kPlus,call}));
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
  RecoveryExpressionInput Input() const {return {plan,equations,order,&original};}
  LegacyFlowProjection Projection() const {
    LegacyFlowProjection result{plan.aliases,plan.literals,plan.replacements,plan.omitted,{},{},plan.statements,private_bindings};
    result.storage=plan.storage;result.read_literals=plan.read_literals;result.transports=plan.transports;
    result.read_bindings=plan.read_bindings;result.read_sources=plan.read_sources;result.original=&original;return result;
  }
};
// A candidate's omitted declaration must not inherit initialization from the
// committed plan supplied to the old flow adapter. Effects must see the same
// candidate completion state. Expected TDZ is derived from no lexical gen.
void CandidatePlan(bool reverse) {
  Fixture f(1,reverse);
  auto stale=f.Projection();const auto omissions=f.plan.omitted;stale.omitted=omissions;
  f.plan.omitted[f.declarations[0].value]=1;
  const auto trial=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,stale,f.Input()));
  Check(!trial.initialized_reads[f.reads[0].value]);
  Check(trial.expressions.evaluations[f.reads[0].value]==ExpressionEvaluation::kRequired);
}
// Each candidate facet replaces a different semantic action. The old adapter
// deliberately retains the committed arrays; expectations come from lexical
// InitializeBinding versus ordinary PutValue and from actual retained calls.
std::pair<std::uint64_t,std::size_t> CandidateDeclarations(bool reverse,unsigned count=1) {
  Fixture f(count,reverse);
  const auto statement_count=f.tree.statements.size();
  f.plan.declaration_values.resize(statement_count);f.plan.declaration_completions.resize(statement_count);
  f.plan.statement_values.resize(statement_count);f.plan.undefined_stores.resize(statement_count);
  const auto declaration=f.declarations[0];
  const auto evaluation=*std::get<DeclarationStmt>(f.tree.statements[declaration.value].statement).initializer;
  // Give the declaration its OWN required call; a candidate must retain it
  // without replaying another statement's owned expression occurrence.
  const ExprId callee{static_cast<unsigned>(f.tree.expressions.size())};
  f.tree.expressions.push_back({SymbolExpr{{0}},{0}});f.equations.push_back(FlowBindingValue{{0}});f.order.push_back(callee);
  f.tree.expressions[evaluation.value].expression=CallExpr{callee,{0,0},CallReference::kValue};
  f.equations[evaluation.value]=UnknownFlowValue{};
  const auto expression_count=f.tree.expressions.size();
  f.plan.read_sources.resize(expression_count);f.plan.read_literals.resize(expression_count);
  f.plan.replacements.resize(expression_count);f.plan.transports.resize(expression_count);
  f.original.normal_kinds.resize(expression_count);f.original.input_ranges.resize(expression_count);
  const auto old=f.Projection();
  const auto binding=std::get<DeclarationStmt>(f.tree.statements[declaration.value].statement).symbol;
  StmtId store{};
  for(unsigned i=0;i<statement_count;++i)if(const auto* expression=std::get_if<ExpressionStmt>(&f.tree.statements[i].statement);
      expression&&expression->expression==f.stores[0])store={i};
  const auto call=std::get<UnaryExpr>(f.tree.expressions[f.values[0].value].expression).operand;
  const auto projected=[&](const RecoveryExpressionInput& input,std::string_view phase) {
    auto result=PrepareRecoveryProjection(f.tree,old,input);
    if(const auto* error=std::get_if<TreeStructureError>(&result)) {
      std::cerr<<"candidate count="<<count<<" phase="<<phase<<" error="<<static_cast<unsigned>(*error)<<'\n';
      Check(false);
    }
    return std::get<RecoveryProjection>(std::move(result));
  };
  auto input=f.Input();
  auto values=f.plan.declaration_values;
  values[declaration.value]=DeclarationValue{evaluation,{}};
  input.bindings.declaration_values=values;
  const auto discarded=projected(input,"discarded");
  Check(!discarded.initialized_reads[f.reads[0].value]);
  Check(discarded.flow.normal_exits[evaluation.value].has_value());
  Check(std::none_of(discarded.flow.definitions.begin(),discarded.flow.definitions.end(),[&](const auto& definition){
    return definition.binding==binding&&definition.declaration==declaration;
  }));
  input=f.Input();auto completions=f.plan.declaration_completions;
  completions[declaration.value]=DeclarationCompletion{binding,evaluation,DeclarationKind::kLet};
  input.bindings.declaration_completions=completions;
  const auto completed=projected(input,"completed");
  Check(completed.initialized_reads[f.reads[0].value]);
  Check(completed.flow.normal_exits[evaluation.value].has_value());
  Check(std::any_of(completed.flow.definitions.begin(),completed.flow.definitions.end(),[&](const auto& definition){
    return definition.binding==binding&&definition.declaration==declaration&&definition.kind==FlowDefinitionKind::kUndefined;
  }));
  input=f.Input();auto initializers=f.plan.statements;auto omitted=f.plan.omitted;
  initializers[store.value]=BindingInitializer{binding,f.stores[0],DeclarationKind::kLet};omitted[declaration.value]=1;
  input.bindings.statements=initializers;input.bindings.omitted=omitted;
  const auto moved=projected(input,"moved");
  Check(moved.initialized_reads[f.reads[0].value]);
  Check(!moved.flow.normal_exits[evaluation.value]);
  Check(std::any_of(moved.flow.definitions.begin(),moved.flow.definitions.end(),[&](const auto& definition){
    return definition.binding==binding&&definition.declaration==store&&definition.kind==FlowDefinitionKind::kInitializer;
  }));
  input=f.Input();auto residuals=f.plan.statement_values;
  residuals[store.value]=std::get<CallExpr>(f.tree.expressions[call.value].expression).callee;
  input.bindings.statement_values=residuals;
  const auto residual=projected(input,"residual");
  Check(!residual.flow.normal_exits[call.value]);
  Check(std::none_of(residual.flow.definitions.begin(),residual.flow.definitions.end(),[&](const auto& definition){
    return definition.binding==binding&&definition.write==f.stores[0];
  }));
  // Views borrow only synchronous owners; neither trial alters the committed
  // plan nor shares its initialized result with a subsequent candidate.
  const auto current=projected(f.Input(),"current");
  Check(current.initialized_reads[f.reads[0].value]);
  Check(!f.plan.omitted[declaration.value]&&!f.plan.statements[store.value]);
  const analysis::SemanticBudget exact{moved.allocated_bytes,moved.work};
  auto moved_input=f.Input();moved_input.bindings.statements=initializers;moved_input.bindings.omitted=omitted;
  Check(std::holds_alternative<RecoveryProjection>(PrepareRecoveryProjection(f.tree,old,moved_input,exact)));
  Check(std::get<TreeStructureError>(PrepareRecoveryProjection(f.tree,old,moved_input,{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(PrepareRecoveryProjection(f.tree,old,moved_input,{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
  return {discarded.work+completed.work+moved.work+residual.work+current.work,
      std::max({discarded.allocated_bytes,completed.allocated_bytes,moved.allocated_bytes,residual.allocated_bytes,current.allocated_bytes})};
}
// A snapshot trial is the final source transaction, including disappearance
// of lexical completion. Its old initializer must not regenerate the removed
// cell or replay the moved call. Retaining a selected component is a no-op.
void SnapshotPlan(bool reverse,bool native,bool outlined=false) {
  Fixture f(1,reverse);
  const auto statements=f.tree.statements.size();
  f.plan.forwarded_values.resize(statements);f.plan.declaration_values.resize(statements);
  f.plan.declaration_completions.resize(statements);
  const auto declaration=f.declarations[0];const SymbolId binding{1};StmtId store{};
  for(unsigned i=0;i<statements;++i)if(const auto* expression=std::get_if<ExpressionStmt>(&f.tree.statements[i].statement);
      expression&&expression->expression==f.stores[0])store={i};
  if(native) {
    f.plan.omitted[declaration.value]=1;
    f.plan.statements[store.value]=BindingInitializer{binding,f.stores[0],DeclarationKind::kLet};
  }
  std::optional<ExprId> residual;
  if(outlined) {
    const auto callee=ExprId{static_cast<unsigned>(f.tree.expressions.size())};
    residual=ExprId{callee.value+1};
    f.tree.expressions.push_back({SymbolExpr{{0}},{0}});
    f.tree.expressions.push_back({CallExpr{callee,{0,0},CallReference::kValue},{0}});
    std::get<DeclarationStmt>(f.tree.statements[declaration.value].statement).initializer=residual;
    f.equations.push_back(FlowBindingValue{{0}});f.equations.push_back(UnknownFlowValue{});
    f.order.insert(f.order.end(),{callee,*residual});
    const auto expressions=f.tree.expressions.size();
    f.plan.read_sources.resize(expressions);f.plan.read_literals.resize(expressions);
    f.plan.replacements.resize(expressions);f.plan.transports.resize(expressions);
    f.original.normal_kinds.resize(expressions);f.original.input_ranges.resize(expressions);
    f.plan.declaration_completions[declaration.value]=DeclarationCompletion{binding,residual,DeclarationKind::kLet};
  }
  const SnapshotMove move{binding,declaration,f.reads[0],f.values[0],f.stores[0],store,store};
  const std::array moves{move};const std::array forward{SnapshotSelection::kForward};
  const auto planned=PlanSnapshotProjection(f.plan,moves,forward,PrimitiveId{0});
  Check(std::holds_alternative<SnapshotProjection>(planned));const auto& delta=std::get<SnapshotProjection>(planned);
  auto input=f.Input();input.bindings=delta.View(f.plan);
  const auto trial=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),input));
  Check(std::none_of(trial.flow.definitions.begin(),trial.flow.definitions.end(),[&](const auto& definition){return definition.binding==binding;}));
  Check(trial.flow.binding_presence[binding.value]==SourceBindingPresence::kElided);
  const auto call=std::get<UnaryExpr>(f.tree.expressions[f.values[0].value].expression).operand;
  Check(std::count_if(trial.flow.events.begin(),trial.flow.events.end(),[&](const auto& event){return event.expression==call&&event.kind==FlowEventKind::kEvaluation;})==1);
  Check(!delta.statements[store.value]&&!delta.completions[store.value]);
  if(residual) {
    Check(!delta.completions[declaration.value]&&delta.values[declaration.value]->value==*residual);
    Check(trial.flow.normal_exits[residual->value].has_value());
    Check(std::count_if(trial.flow.events.begin(),trial.flow.events.end(),[&](const auto& event){return event.expression==*residual&&event.kind==FlowEventKind::kEvaluation;})==1);
    Check(f.plan.declaration_completions[declaration.value].has_value());
  }
  Check(f.plan.statements[store.value].has_value()==native&&!f.plan.replacements[f.reads[0].value]);
  const std::array retained{SnapshotSelection::kRetain};
  const auto kept=std::get<SnapshotProjection>(PlanSnapshotProjection(f.plan,moves,retained,PrimitiveId{0}));
  Check(kept.statements[store.value].has_value()==native&&!kept.replacements[f.reads[0].value]);
  const analysis::SemanticBudget exact{delta.allocated_bytes,delta.work};
  Check(std::holds_alternative<SnapshotProjection>(PlanSnapshotProjection(f.plan,moves,forward,PrimitiveId{0},exact)));
  Check(std::get<TreeStructureError>(PlanSnapshotProjection(f.plan,moves,forward,PrimitiveId{0},{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(PlanSnapshotProjection(f.plan,moves,forward,PrimitiveId{0},{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
  Check(std::get<TreeStructureError>(PlanSnapshotProjection(f.plan,moves,{},PrimitiveId{0}))==TreeStructureError::kOwnership);
  auto published=std::get<SnapshotProjection>(PlanSnapshotProjection(f.plan,moves,forward,PrimitiveId{0}));
  std::move(published).Publish(f.plan);
  const auto committed=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input()));
  Check(std::none_of(committed.flow.definitions.begin(),committed.flow.definitions.end(),[&](const auto& definition){return definition.binding==binding;}));
  Check(std::count_if(committed.flow.events.begin(),committed.flow.events.end(),[&](const auto& event){return event.expression==call&&event.kind==FlowEventKind::kEvaluation;})==1);
  if(residual)Check(committed.flow.normal_exits[residual->value].has_value());

}
std::pair<std::uint64_t,std::size_t> SnapshotBatch(unsigned count,bool reverse) {
  Fixture f(count,reverse);const auto statements=f.tree.statements.size();
  f.plan.forwarded_values.resize(statements);f.plan.declaration_values.resize(statements);f.plan.declaration_completions.resize(statements);
  std::vector<std::optional<StmtId>> owners(f.tree.expressions.size());
  for(unsigned i=0;i<statements;++i)if(const auto* expression=std::get_if<ExpressionStmt>(&f.tree.statements[i].statement))owners[expression->expression.value]=StmtId{i};
  std::vector<SnapshotMove> moves;std::vector<SnapshotSelection> selections(count,SnapshotSelection::kForward);
  for(unsigned i=0;i<count;++i)moves.push_back({{i+1},f.declarations[i],f.reads[i],f.values[i],f.stores[i],*owners[f.stores[i].value],{}});
  const auto delta=std::get<SnapshotProjection>(PlanSnapshotProjection(f.plan,moves,selections,PrimitiveId{0}));
  auto input=f.Input();input.bindings=delta.View(f.plan);
  const auto trial=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),input));
  Check(std::none_of(trial.flow.definitions.begin(),trial.flow.definitions.end(),[](const auto& definition){return definition.binding!=SymbolId{0};}));
  std::vector<unsigned> evaluations(f.tree.expressions.size());
  for(const auto& event:trial.flow.events)if(event.expression&&event.kind==FlowEventKind::kEvaluation)++evaluations[event.expression->value];
  for(unsigned i=0;i<count;++i) {
    Check(trial.flow.binding_presence[i+1]==SourceBindingPresence::kElided);
    const auto call=std::get<UnaryExpr>(f.tree.expressions[f.values[i].value].expression).operand;
    Check(evaluations[call.value]==1);
  }
  const analysis::SemanticBudget exact{delta.allocated_bytes,delta.work};
  Check(std::holds_alternative<SnapshotProjection>(PlanSnapshotProjection(f.plan,moves,selections,PrimitiveId{0},exact)));
  Check(std::get<TreeStructureError>(PlanSnapshotProjection(f.plan,moves,selections,PrimitiveId{0},{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(PlanSnapshotProjection(f.plan,moves,selections,PrimitiveId{0},{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
  return {delta.work+trial.work,delta.allocated_bytes+trial.allocated_bytes};
}
// Hand-checked namespace graph: report's fixed inner shadow rejects move 3;
// restoring move 3's input shadow then rejects every saved move. An independent
// report lookup outside that shadow remains movable. No solver builds expected
// selections. Binding/expression IDs are permuted independently of regions.
std::pair<std::uint64_t,std::size_t> SnapshotRestoration(unsigned count,bool reverse,bool nested,bool free,bool storage=false) {
  RecoveryFlow current{},draft{};const unsigned bindings=count+5,expressions=count+2;
  const auto id=[&](unsigned i){return SymbolId{reverse?bindings-1-i:i};};
  const auto expr=[&](unsigned i){return ExprId{reverse?expressions-1-i:i};};
  current.bindings.resize(bindings);current.binding_presence.assign(bindings,SourceBindingPresence::kRetained);
  current.source_spellings.resize(bindings);current.source_name_spellings={SourceSpellingId{0},SourceSpellingId{1}};
  current.source_regions={{{0},{10}},{{2},{8}},{{3},{7}}};
  current.source_occurrences={SourcePosition{5},SourcePosition{1}};
  for(unsigned i=0;i<bindings;++i) {
    current.bindings[id(i).value].scope=OriginalBindingScope{SourceRegionId{i==2?2U:i==3?1U:0U}};
    current.source_spellings[id(i).value]={i==3?0U:i==2?1U:i<2?i:i-2};
  }
  current.source_shadows={{id(0),SourceRegionId{1},id(3)},{id(1),SourceRegionId{2},id(2)}};
  if(free) {
    current.source_spellings[id(0).value]={count+3};current.source_spellings[id(1).value]={count+4};
    current.source_shadows.clear();
  }
  if(storage)current.binding_presence[id(2).value]=SourceBindingPresence::kElided;
  auto names=std::get<SourceNameFacts>(AnalyzeSourceNames(current,current.binding_presence,current.source_shadows));
  current.source_names=std::move(names.index);
  draft.expression_transports.resize(expressions);std::vector<SnapshotMove> moves;
  for(unsigned i=0;i<expressions;++i) {
    const auto binding=id(i==0?3U:i<count+1?i+3:count+4);
    const auto root=nested&&i==expressions-1?id(3):binding;
    draft.expression_transports[expr(i).value]=root;
    const auto source=storage&&i==expressions-1?id(3):i==0||i==expressions-1?id(1):id(0);
    const auto owner=StmtId{i==expressions-1&&!storage?1U:0U};
    if(free)draft.names.push_back({{0},NameId{source==id(0)?0U:1U},expr(i),owner});
    else draft.references.push_back({{0},source,FlowReferenceKind::kRead,expr(i),{},owner,{}});
    moves.push_back({binding,{0},expr(i),expr(i),{},{0},{}});
  }
  const auto analyzed=PlanSnapshotRetention(current,draft,moves,{});
  Check(std::holds_alternative<SnapshotRetention>(analyzed));const auto& result=std::get<SnapshotRetention>(analyzed);
  for(unsigned i=0;i<expressions-1;++i)Check(result.selections[i]==SnapshotSelection::kRetain);
  Check(result.selections.back()==(nested||storage?SnapshotSelection::kRetain:SnapshotSelection::kForward));
  const analysis::SemanticBudget exact{result.allocated_bytes,result.work};
  Check(std::holds_alternative<SnapshotRetention>(PlanSnapshotRetention(current,draft,moves,{},exact)));
  Check(std::get<TreeStructureError>(PlanSnapshotRetention(current,draft,moves,{},{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(PlanSnapshotRetention(current,draft,moves,{},{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
  current.source_names.reset();const auto unknown=std::get<SnapshotRetention>(PlanSnapshotRetention(current,draft,moves,{}));
  for(const auto selection:unknown.selections)Check(selection==SnapshotSelection::kRetain);
  return {result.work,result.allocated_bytes};
}
void Case(bool reverse) {
  Fixture f(1,reverse);
  const auto current=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input()));
  Check(current.initialized_reads[f.reads[0].value]);
  Check(current.expressions.evaluations[f.results[0].value]==ExpressionEvaluation::kInert);
  auto stale=f.Input();stale.normal_kinds=current.expressions.kinds;
  Check(std::get<TreeStructureError>(PrepareRecoveryProjection(f.tree,f.Projection(),stale))==TreeStructureError::kOwnership);
  f.plan.replacements[f.reads[0].value]=std::get<UnaryExpr>(f.tree.expressions[f.values[0].value].expression).operand;
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
  Check(trial.flow.normal_exits[std::get<UnaryExpr>(f.tree.expressions[f.values[0].value].expression).operand.value].has_value());
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
  f.plan.replacements[f.reads[0].value]=std::get<UnaryExpr>(f.tree.expressions[f.values[0].value].expression).operand;f.plan.transports[f.stores[0].value]=ExpressionTransport{f.values[0],PrimitiveId{0}};
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
void ChangedDefinition(bool reverse) {
  Fixture f(1,reverse);
  // The original successful operation produces Number; replacing the written
  // value with a parameter must invalidate downstream raw operand domains.
  const auto call=std::get<UnaryExpr>(f.tree.expressions[f.values[0].value].expression).operand;
  const auto unknown=std::get<CallExpr>(f.tree.expressions[call.value].expression).callee;
  f.original.normal_kinds[f.values[0].value]=analysis::ValueKinds::Number();
  f.plan.replacements[f.values[0].value]=unknown;
  const auto trial=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input()));
  Check(trial.expressions.kinds[f.reads[0].value]==analysis::ValueKinds::Language());
  Check(trial.expressions.evaluations[f.results[0].value]==ExpressionEvaluation::kRequired);
}
void OriginalEntries(bool reverse) {
  Fixture f(1,reverse);
  const auto call=std::get<UnaryExpr>(f.tree.expressions[f.values[0].value].expression).operand;
  const auto entry=std::get<CallExpr>(f.tree.expressions[call.value].expression).callee;
  f.original.values.push_back({analysis::EntryDefinition{analysis::ParameterEntry{0}},analysis::ValueKinds::Number()});
  std::vector<std::uint8_t> stable{1,0};auto projection=f.Projection();projection.stable_entries=stable;
  const auto typed=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,projection,f.Input()));
  Check(typed.expressions.kinds[entry.value].IsNumber());
  stable[0]=0;
  const auto exposed=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,projection,f.Input()));
  Check(exposed.expressions.kinds[entry.value]==analysis::ValueKinds::Language());
  stable[0]=1;f.original.values[0].domain=analysis::ValueKinds::Language();
  const auto unknown=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,projection,f.Input()));
  Check(unknown.expressions.kinds[entry.value]==analysis::ValueKinds::Language());
  std::get<analysis::ParameterEntry>(std::get<analysis::EntryDefinition>(f.original.values[0].origin)).index=1;
  Check(std::get<TreeStructureError>(PrepareRecoveryProjection(f.tree,projection,f.Input()))==TreeStructureError::kOwnership);
}
// Deliberately slow small-graph oracle: whole-state rescans and a separately
// written five-element lattice. It does not reuse production liveness, sparse
// dependencies or source type transfer to compute the expected read domains.
void KindOracle(unsigned seed,bool reverse) {
  enum class Type {kBottom,kNumber,kString,kDefined,kLanguage};
  const auto join=[](Type a,Type b) {
    if(a==Type::kBottom)return b;if(b==Type::kBottom||a==b)return a;
    if(a==Type::kLanguage||b==Type::kLanguage)return Type::kLanguage;
    return Type::kDefined;
  };
  const auto kinds=[](Type value) {
    using K=analysis::ValueKinds;
    switch(value) {
      case Type::kBottom:return K{};case Type::kNumber:return K::Number();
      case Type::kString:return K::String();case Type::kDefined:return K::Defined();
      case Type::kLanguage:return K::Language();
    }
    std::abort();
  };
  Fixture f(1,reverse);RecoveryFlow flow{};flow.original=&f.original;
  flow.bindings.resize(2);flow.bindings[0]={BindingKind::kParameter};
  flow.bindings[1]={BindingKind::kCompilerTemporary,FlowBindingExposure::kPrivate};
  constexpr unsigned points=8;
  flow.events.resize(points);
  const auto point=[&](unsigned i){return FlowPoint{reverse?points-1-i:i};};
  flow.entry=point(0);flow.terminal=point(points-1);
  const auto expr=[&](TreeExpression node,FlowValueEquation equation) {
    ExprId id{static_cast<unsigned>(f.tree.expressions.size())};f.tree.expressions.push_back({std::move(node),{0}});
    f.equations.push_back(equation);f.order.push_back(id);return id;
  };
  const auto number=expr(TreeLiteral{IntegerLiteral{9}},FlowPrimitiveReference{{1}});
  f.tree.texts.push_back(u"value");f.plan.primitives.push_back(TextId{0});
  const auto string=expr(TreeLiteral{TextId{0}},FlowPrimitiveReference{{2}});
  const auto unknown=expr(SymbolExpr{{0}},FlowBindingValue{{0}});
  std::vector<Type> definitions(points,Type::kBottom),incoming(points,Type::kBottom);
  std::vector<std::optional<ExprId>> reads(points);
  const auto random=[&] {seed=seed*1664525u+1013904223u;return seed;};
  for(unsigned i=0;i<points;++i) {
    const auto choice=i==0?0u:random()%5;
    if(choice<3) {
      definitions[i]=choice==0?Type::kNumber:choice==1?Type::kString:Type::kLanguage;
      const auto value=choice==0?number:choice==1?string:unknown;
      const FlowDefinitionId id{static_cast<unsigned>(flow.definitions.size())};
      flow.definitions.push_back({point(i),{1},FlowDefinitionKind::kAssignment,{},value});
      flow.events[point(i).value].definition=id;flow.events[point(i).value].kind=FlowEventKind::kDefinition;
    } else {
      reads[i]=expr(SymbolExpr{{1}},FlowBindingValue{{1}});
      flow.events[point(i).value].read=SymbolId{1};flow.events[point(i).value].kind=FlowEventKind::kRead;
      flow.references.push_back({point(i),{1},FlowReferenceKind::kRead,*reads[i]});
    }
  }
  struct Edge {unsigned from,to;};std::vector<Edge> edges;
  for(unsigned i=0;i+1<points;++i)edges.push_back({i,i+1});
  for(unsigned i=1;i<points;++i)if(random()%2)edges.push_back({i,random()%points});
  for(const auto& edge:edges) {
    const auto from=point(edge.from),to=point(edge.to);
    flow.edges.push_back({from,to,edge.from<edge.to?FlowCompletion::kNormal:FlowCompletion::kThrow,
        flow.events[to.value].incoming,flow.events[from.value].outgoing});
    flow.events[to.value].incoming=flow.events[from.value].outgoing=flow.edges.size()-1;
  }
  incoming[0]=Type::kLanguage;
  bool changed=true;
  while(changed) {
    changed=false;
    for(const auto& edge:edges) {
      const auto out=definitions[edge.from]==Type::kBottom?incoming[edge.from]:definitions[edge.from];
      const auto merged=join(incoming[edge.to],out);
      if(incoming[edge.to]!=merged){incoming[edge.to]=merged;changed=true;}
    }
  }
  const auto size=f.tree.expressions.size();
  f.plan.replacements.resize(size);f.plan.transports.resize(size);f.plan.read_sources.resize(size);f.plan.read_literals.resize(size);
  f.original.normal_kinds.resize(size);f.original.input_ranges.resize(size);
  // Poison canonical read hints deliberately; the projected reaching values
  // must determine the answer even when every old occurrence said Number.
  for(const auto read:reads)if(read)f.original.normal_kinds[read->value]=analysis::ValueKinds::Number();
  const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(flow));
  const auto actual=std::get<FlowNormalKinds>(AnalyzeFlowNormalKinds(f.tree,flow,live,f.Input()));
  for(unsigned i=0;i<points;++i)if(reads[i])Check(actual.expressions[reads[i]->value]==kinds(incoming[i]));
  const analysis::SemanticBudget exact{actual.allocated_bytes,actual.work};
  Check(std::holds_alternative<FlowNormalKinds>(AnalyzeFlowNormalKinds(f.tree,flow,live,f.Input(),exact)));
  Check(std::get<TreeStructureError>(AnalyzeFlowNormalKinds(f.tree,flow,live,f.Input(),{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(AnalyzeFlowNormalKinds(f.tree,flow,live,f.Input(),{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
}
void CompletionTypes(unsigned form,bool reverse,bool emit) {
  Fixture f(1,reverse);auto& tree=f.tree;
  const auto stmt=[&](TreeStatement node) {
    StmtId id{static_cast<unsigned>(tree.statements.size())};tree.statements.push_back({std::move(node),{0}});return id;
  };
  const auto expr=[&](TreeExpression node,FlowValueEquation equation=UnknownFlowValue{}) {
    ExprId id{static_cast<unsigned>(tree.expressions.size())};tree.expressions.push_back({std::move(node),{0}});
    f.equations.push_back(equation);f.order.push_back(id);return id;
  };
  const auto scope=[&](ScopeKind kind=ScopeKind::kBlock,ScopeId parent=ScopeId{1}) {
    ScopeId id{static_cast<unsigned>(tree.scopes.size())};tree.scopes.push_back({kind,parent});return id;
  };
  const auto block=[&](std::vector<StmtId> children,ScopeId region) {
    const auto first=tree.children.size();tree.children.insert(tree.children.end(),children.begin(),children.end());
    return stmt(BlockStmt{region,static_cast<unsigned>(first),static_cast<unsigned>(children.size())});
  };
  const bool numeric=form%2==0,loop=form>=2;
  if(!numeric) {
    const auto call=std::get<UnaryExpr>(tree.expressions[f.values[0].value].expression).operand;
    const auto ignored=expr(TreeLiteral{UndefinedLiteral{}},FlowPrimitiveReference{{0}});
    tree.expressions[f.values[0].value].expression=SequenceExpr{ignored,call};
    f.equations[f.values[0].value]=FlowExpressionValue{call};
  }
  const auto body=tree.children;tree.children.clear();
  const auto try_scope=scope();
  StmtId protected_body;
  if(loop) {
    const auto repeated=stmt(LoopStmt{});const auto loop_scope=scope(ScopeKind::kBlock,try_scope);
    const auto iteration_scope=scope(ScopeKind::kBlock,loop_scope);
    const auto input=expr(SymbolExpr{{0}},FlowBindingValue{{0}});
    const auto condition=expr(CallExpr{input,{0,0},CallReference::kValue});
    const auto choose=stmt(IfStmt{condition,block({body[1],body[2]},iteration_scope),
        block({stmt(BreakStmt{repeated,{}})},scope(ScopeKind::kBlock,loop_scope))});
    std::get<LoopStmt>(tree.statements[repeated.value].statement).body=block({choose},loop_scope);
    protected_body=block({repeated},try_scope);
  } else protected_body=block({body[1]},try_scope);
  const auto catch_scope=scope(ScopeKind::kCatch);
  tree.names.push_back(u"error");tree.symbols.push_back({catch_scope,{static_cast<unsigned>(tree.names.size()-1)},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
  const auto catch_read=expr(SymbolExpr{{1}},FlowBindingValue{{1}});
  const auto handler=block({stmt(ExpressionStmt{expr(UnaryExpr{TreeUnary::kNegate,catch_read})})},catch_scope);
  const auto attempt=stmt(TryStmt{protected_body,CatchClause{{2},handler},{}});
  const auto returned=stmt(ReturnStmt{expr(SymbolExpr{{1}},FlowBindingValue{{1}})});
  // Distinct source occurrences: the loop owns the old discard; the outside
  // observation gets its own read instead of sharing syntax nodes.
  auto after=body[2];ExprId after_read=f.reads[0];
  if(loop) {
    after_read=expr(SymbolExpr{{1}},FlowBindingValue{{1}});
    after=stmt(ExpressionStmt{expr(UnaryExpr{TreeUnary::kNegate,after_read})});
  }
  const auto first=tree.children.size();tree.children.insert(tree.children.end(),{body[0],attempt,after,returned});
  std::get<BlockStmt>(tree.statements[0].statement)={tree.body_scope,static_cast<unsigned>(first),4};
  const auto bindings=tree.symbols.size(),expressions=tree.expressions.size(),statements=tree.statements.size();
  f.plan.aliases.resize(bindings);f.plan.storage.resize(bindings);f.plan.read_bindings.resize(bindings);f.plan.literals.resize(bindings);f.private_bindings.resize(bindings,1);
  f.plan.replacements.resize(expressions);f.plan.transports.resize(expressions);f.plan.read_sources.resize(expressions);f.plan.read_literals.resize(expressions);
  f.plan.omitted.resize(statements);f.plan.statements.resize(statements);
  f.original.bindings=bindings;f.original.statements=statements;f.original.normal_kinds.resize(expressions);f.original.input_ranges.resize(expressions);
  f.original.normal_kinds[after_read.value]=analysis::ValueKinds::Number();
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto actual=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,f.Projection(),f.Input()));
  Check(actual.expressions.kinds[after_read.value].IsNumber()==numeric);
  Check(actual.expressions.kinds[catch_read.value].IsNumber()==(numeric||!loop));
  Check(actual.expressions.evaluations[std::get<ExpressionStmt>(tree.statements[after.value].statement).expression.value]==
      (numeric?ExpressionEvaluation::kInert:ExpressionEvaluation::kRequired));
  std::ostringstream output;
  // Source-adapter fixture only: it makes no claim to an original ABC/SSA
  // certificate. Actual decoded originals remain in function_tree_test.
  const auto printed=PrintFunctionTree(tree,output,{}, {},nullptr,&f.original);
  Check(std::holds_alternative<PrintedFunction>(printed));
  if(emit)std::cout<<nlohmann::json{{"form",form},{"reverse",reverse},{"source",output.str()}}<<'\n';
}

void ScopeExitSource(unsigned form,unsigned copies,bool split,bool emit,bool narrowed=false) {
  const bool loop=form==0||form==1,thrown=form==2;
  FunctionTree tree{};tree.callable=CallableKind::kFunction;tree.strictness=Strictness::kStrict;
  tree.parameter_scope={0};tree.body_scope={1};tree.body={0};tree.origins={OriginalSite{{0},{0}}};
  tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
  const auto scope=[&](ScopeId parent,ScopeKind kind=ScopeKind::kBlock){ScopeId id{static_cast<unsigned>(tree.scopes.size())};tree.scopes.push_back({kind,parent});return id;};
  const auto repeated_scope=loop?scope({1}):ScopeId{1};
  auto enclosing=split?scope(repeated_scope):repeated_scope;const auto local_scope=scope(enclosing);
  tree.names={u"input",u"saved",u"local"};
  tree.symbols={{{0},{0},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}},
      {{1},{1},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}},
      {narrowed?ScopeId{1}:local_scope,{2},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}}};
  tree.parameters={{{0},{},false}};
  const auto expr=[&](TreeExpression node){ExprId id{static_cast<unsigned>(tree.expressions.size())};tree.expressions.push_back({std::move(node),{0}});return id;};
  const auto stmt=[&](TreeStatement node){StmtId id{static_cast<unsigned>(tree.statements.size())};tree.statements.push_back({std::move(node),{0}});return id;};
  const auto block=[&](ScopeId at,const std::vector<StmtId>& children){const auto first=tree.children.size();tree.children.insert(tree.children.end(),children.begin(),children.end());return stmt(BlockStmt{at,static_cast<unsigned>(first),static_cast<unsigned>(children.size())});};
  const auto call=[&](SymbolId callee){return expr(CallExpr{expr(SymbolExpr{callee}),{0,0},CallReference::kValue});};
  const auto root=stmt(BlockStmt{});const auto saved=stmt(DeclarationStmt{{1},{}});
  std::optional<StmtId> repeated;if(loop)repeated=stmt(LoopStmt{});
  std::vector<StmtId> local_statements;SymbolId callee{0};
  for(unsigned i=0;i<copies;++i) {
    const SymbolId copied{static_cast<unsigned>(tree.symbols.size())};const NameId name{static_cast<unsigned>(tree.names.size())};
    const auto suffix=std::to_string(i);tree.names.push_back(u"copy"+std::u16string(suffix.begin(),suffix.end()));tree.symbols.push_back({local_scope,name,BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
    local_statements.push_back(stmt(DeclarationStmt{copied,expr(SymbolExpr{callee})}));callee=copied;
  }
  if(narrowed)local_statements.push_back(stmt(ExpressionStmt{expr(AssignmentExpr{SymbolTarget{{2}},call(callee)})}));
  else local_statements.push_back(stmt(DeclarationStmt{{2},call(callee)}));
  local_statements.push_back(stmt(ExpressionStmt{expr(AssignmentExpr{SymbolTarget{{1}},expr(SymbolExpr{{2}})})}));
  if(loop)local_statements.push_back(form==0?stmt(BreakStmt{*repeated,{}}):stmt(ContinueStmt{*repeated,{}}));
  else if(thrown)local_statements.push_back(stmt(ThrowStmt{call({0})}));
  else local_statements.push_back(stmt(ReturnStmt{expr(SymbolExpr{{2}})}));
  auto body=block(local_scope,local_statements);if(split)body=block(enclosing,{body});
  StmtId entry=body;
  if(loop) {
    const auto stop=block(scope(repeated_scope),{stmt(BreakStmt{*repeated,{}})});
    const auto choose=stmt(IfStmt{call({0}),body,stop});
    std::get<LoopStmt>(tree.statements[repeated->value].statement).body=block(repeated_scope,{choose});entry=*repeated;
  } else if(thrown) {
    const auto caught_scope=scope({1},ScopeKind::kCatch);const SymbolId error{static_cast<unsigned>(tree.symbols.size())};
    const NameId name{static_cast<unsigned>(tree.names.size())};tree.names.push_back(u"error");tree.symbols.push_back({caught_scope,name,BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
    tree.arguments={expr(SymbolExpr{error}),expr(SymbolExpr{{1}})};
    const auto caught=block(caught_scope,{stmt(ExpressionStmt{expr(CallExpr{expr(SymbolExpr{{0}}),{0,2},CallReference::kValue})})});
    entry=stmt(TryStmt{body,CatchClause{error,caught},{}});
  }
  std::vector<StmtId> statements{saved};if(narrowed)statements.push_back(stmt(DeclarationStmt{{2},{}}));statements.push_back(entry);if(form!=3)statements.push_back(stmt(ReturnStmt{expr(SymbolExpr{{1}})}));
  const auto first=tree.children.size();tree.children.insert(tree.children.end(),statements.begin(),statements.end());
  std::get<BlockStmt>(tree.statements[root.value].statement)={{1},static_cast<unsigned>(first),static_cast<unsigned>(statements.size())};
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  std::ostringstream output;const auto printed=PrintFunctionTree(tree,output);
  Check(std::holds_alternative<PrintedFunction>(printed));const auto& facts=std::get<PrintedFunction>(printed);Check(!facts.incomplete);
  if(emit)std::cout<<nlohmann::json{{"form",form},{"copies",copies},{"split",split},{"source",output.str()},
      {"bindings",tree.symbols.size()},{"statements",tree.statements.size()},{"expressions",tree.expressions.size()},
      {"work",facts.work},{"allocated_bytes",facts.allocated_bytes}}<<'\n';
}
void CapturedKinds(bool reverse) {
  Fixture f(1,reverse);RecoveryFlow flow{};flow.original=&f.original;flow.events.resize(5);flow.bindings.resize(2);
  flow.bindings[0].kind=BindingKind::kParameter;
  flow.bindings[1].kind=BindingKind::kCompilerTemporary;flow.bindings[1].exposure=FlowBindingExposure::kPrivate;
  const auto point=[&](unsigned i){return FlowPoint{reverse?4-i:i};};flow.entry=point(0);flow.bindings[1].allocation=point(2);
  flow.definitions={{point(0),{1},FlowDefinitionKind::kInitializer,{},f.values[0]},
      {point(3),{1},FlowDefinitionKind::kIterationCopy}};
  flow.definitions[1].input=FlowCapturedValue{point(1),{1}};
  flow.events[point(0).value].definition=FlowDefinitionId{0};flow.events[point(3).value].definition=FlowDefinitionId{1};
  flow.events[point(1).value].kind=FlowEventKind::kCopyInput;flow.events[point(1).value].read=SymbolId{1};
  flow.events[point(4).value].kind=FlowEventKind::kRead;flow.events[point(4).value].read=SymbolId{1};
  flow.references={{point(4),{1},FlowReferenceKind::kRead,f.reads[0]}};
  for(unsigned i=0;i<4;++i) {
    const auto from=point(i),to=point(i+1);
    flow.edges.push_back({from,to,FlowCompletion::kNormal,flow.events[to.value].incoming,flow.events[from.value].outgoing});
    flow.events[to.value].incoming=flow.events[from.value].outgoing=flow.edges.size()-1;
  }
  const auto solve=[&]{return std::get<FlowNormalKinds>(AnalyzeFlowNormalKinds(f.tree,flow,std::get<FlowLiveness>(AnalyzeFlowLiveness(flow)),f.Input()));};
  Check(solve().expressions[f.reads[0].value].IsNumber());
  flow.definitions[1].input=FlowCapturedValue{point(3),{1}};
  Check(solve().expressions[f.reads[0].value]==analysis::ValueKinds::Language());
}
void ActivationKinds(bool reverse) {
  Fixture fixture(1,reverse);
  auto projection=std::get<RecoveryProjection>(PrepareRecoveryProjection(fixture.tree,fixture.Projection(),fixture.Input()));
  const auto boundary=projection.flow.statements[fixture.tree.children[2].value];
  const ScopeId scope{2};projection.flow.bindings[1].lifetime=ScopeBindingLifetime{scope,boundary,boundary};
  auto& event=projection.flow.events[boundary.value];event.kind=FlowEventKind::kEnterScope;event.scope=scope;
  const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(projection.flow));
  const auto actual=std::get<FlowNormalKinds>(AnalyzeFlowNormalKinds(fixture.tree,projection.flow,live,fixture.Input()));
  Check(actual.expressions[fixture.reads[0].value]==analysis::ValueKinds::Language());
}
void ScopedExposedKinds(bool reverse) {
  Fixture f(1,reverse);f.private_bindings[1]=0;
  f.tree.symbols[1].kind=BindingKind::kLocal;f.tree.symbols[1].temporary.reset();
  // A distinct initialized GetValue separates the store from the observed
  // read. Its impossible TDZ completion must be refined before value solving.
  const ExprId guard{static_cast<unsigned>(f.tree.expressions.size())};
  f.tree.expressions.push_back({SymbolExpr{{1}},{0}});f.equations.push_back(FlowBindingValue{{1}});f.order.push_back(guard);
  const StmtId guard_statement{static_cast<unsigned>(f.tree.statements.size())};
  f.tree.statements.push_back({ExpressionStmt{guard},{0}});f.tree.children.insert(f.tree.children.begin()+2,guard_statement);
  ++std::get<BlockStmt>(f.tree.statements[0].statement).child_count;
  f.plan.read_sources.resize(f.tree.expressions.size());f.plan.read_literals.resize(f.tree.expressions.size());
  f.plan.replacements.resize(f.tree.expressions.size());f.plan.transports.resize(f.tree.expressions.size());
  f.plan.omitted.resize(f.tree.statements.size());f.plan.statements.resize(f.tree.statements.size());
  f.original.statements=f.tree.statements.size();f.original.normal_kinds.resize(f.tree.expressions.size());f.original.input_ranges.resize(f.tree.expressions.size());
  auto model=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input()));
  Check(std::holds_alternative<ScopeBindingLifetime>(model.flow.bindings[1].lifetime));
  Check(model.expressions.kinds[f.reads[0].value].IsNumber());
  bool observable=false;
  for(const auto& definition:model.flow.definitions)if(definition.binding==SymbolId{1}) {
    Check(definition.effect==FlowWriteEffect::kObservable);observable=true;
  }
  Check(observable);
  const auto read=std::find_if(model.flow.references.begin(),model.flow.references.end(),[&](const auto& reference){
    return reference.kind==FlowReferenceKind::kRead&&reference.site==FlowReferenceSite{f.reads[0]};
  });
  Check(read!=model.flow.references.end()&&model.flow.events[read->point.value].incoming);
  const auto before=model.flow.edges[*model.flow.events[read->point.value].incoming].from;
  auto& event=model.flow.events[before.value];Check(!event.definition&&event.effect==ExpressionEvaluation::kInert);
  // Small reference state: a normal numeric store establishes Number; an
  // unknown user evaluation may replace an exposed unit with any value.
  // This is a model counterexample, not an original-SSA or closure certificate.
  event.effect=ExpressionEvaluation::kRequired;
  const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(model.flow));
  const auto changed=std::get<FlowNormalKinds>(AnalyzeFlowNormalKinds(f.tree,model.flow,live,f.Input()));
  Check(changed.expressions[f.reads[0].value]==analysis::ValueKinds::Language());
  event.effect=ExpressionEvaluation::kInert;
  Check(std::get<FlowNormalKinds>(AnalyzeFlowNormalKinds(f.tree,model.flow,live,f.Input())).expressions[f.reads[0].value].IsNumber());
}
void ActivationSource(unsigned copies,bool split,bool emit) {
  FunctionTree tree{};tree.callable=CallableKind::kFunction;tree.strictness=Strictness::kStrict;
  tree.parameter_scope={0};tree.body_scope={1};tree.body={0};tree.origins={OriginalSite{{0},{0}}};
  tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}},
      {ScopeKind::kBlock,ScopeId{1}},{ScopeKind::kBlock,ScopeId{2}},
      {ScopeKind::kBlock,ScopeId{3}},{ScopeKind::kCatch,ScopeId{3}},
      {ScopeKind::kBlock,ScopeId{2}}};
  tree.names={u"input",u"error",u"local"};
  tree.symbols={{{0},{0},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}},
      {{5},{1},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}},
      {{5},{2},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}}};
  tree.parameters={{{0},{},false}};
  const auto expr=[&](TreeExpression node){ExprId id{static_cast<unsigned>(tree.expressions.size())};tree.expressions.push_back({std::move(node),{0}});return id;};
  const auto stmt=[&](TreeStatement node){StmtId id{static_cast<unsigned>(tree.statements.size())};tree.statements.push_back({std::move(node),{0}});return id;};
  const auto block=[&](ScopeId scope,const std::vector<StmtId>& children) {
    const auto first=tree.children.size();tree.children.insert(tree.children.end(),children.begin(),children.end());
    return stmt(BlockStmt{scope,static_cast<unsigned>(first),static_cast<unsigned>(children.size())});
  };
  const auto root=stmt(BlockStmt{}),loop=stmt(LoopStmt{});
  const auto call=[&](SymbolId callee){return expr(CallExpr{expr(SymbolExpr{callee}),{0,0},CallReference::kValue});};
  const auto condition=call({0});const auto attempt=block({4},{stmt(ExpressionStmt{call({0})})});
  std::vector<StmtId> handler{stmt(DeclarationStmt{{2},{}})};SymbolId callee{0};
  for(unsigned i=0;i<copies;++i) {
    const SymbolId copied{static_cast<unsigned>(tree.symbols.size())};
    const NameId name{static_cast<unsigned>(tree.names.size())};tree.names.push_back(u"copy"+std::u16string(i+1,u'x'));
    tree.symbols.push_back({{5},name,BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
    handler.push_back(stmt(DeclarationStmt{copied,expr(SymbolExpr{callee})}));callee=copied;
  }
  const auto error=expr(SymbolExpr{{1}}),local=expr(SymbolExpr{{2}});
  tree.arguments={error,local};
  handler.push_back(stmt(ExpressionStmt{expr(CallExpr{expr(SymbolExpr{callee}),{0,2},CallReference::kValue})}));
  const auto caught=block({5},handler);
  auto iteration=block({3},{stmt(TryStmt{attempt,CatchClause{{1},caught},{}})});
  if(split) {
    const ScopeId enclosing{static_cast<unsigned>(tree.scopes.size())};
    tree.scopes.push_back({ScopeKind::kBlock,ScopeId{2}});tree.scopes[3].parent=enclosing;
    iteration=block(enclosing,{iteration});
  }
  const auto stopped=block({6},{stmt(BreakStmt{loop,{}})});
  const auto choose=stmt(IfStmt{condition,iteration,stopped});
  std::get<LoopStmt>(tree.statements[loop.value].statement).body=block({2},{choose});
  const auto first=tree.children.size();tree.children.push_back(loop);
  std::get<BlockStmt>(tree.statements[root.value].statement)={{1},static_cast<unsigned>(first),1};
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  if(emit)std::cout<<nlohmann::json{{"copies",copies},{"split",split},{"source",output.str()}}<<'\n';
}

}
int main(int argc,char** argv) {
  for(bool reverse:{false,true}){CandidatePlan(reverse);CandidateDeclarations(reverse);SnapshotPlan(reverse,false);SnapshotPlan(reverse,true);SnapshotPlan(reverse,false,true);}
  for(bool reverse:{false,true})for(bool nested:{false,true})for(bool free:{false,true})SnapshotRestoration(1,reverse,nested,free);
  for(bool reverse:{false,true})SnapshotRestoration(1,reverse,false,false,true);
  if(argc==2&&std::string_view(argv[1])=="--snapshot-plan") {
    std::uint64_t previous=0,previous_projection=0;
    for(unsigned count:{200U,201U,400U,800U}) {
      const auto [work,bytes]=SnapshotRestoration(count,false,false,false);
      Check(!previous||work<previous*3);previous=work;
      SnapshotRestoration(count,true,false,true);
      const auto [projection_work,projection_bytes]=SnapshotBatch(count,false);SnapshotBatch(count,true);
      Check(!previous_projection||projection_work<previous_projection*3);previous_projection=projection_work;
      std::cout<<"snapshot retirement count="<<count<<" work="<<work<<" bytes="<<bytes<<" projection_work="<<projection_work<<" projection_bytes="<<projection_bytes<<'\n';
    }
    return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--candidate-plan")return 0;
  if(argc==2&&std::string_view(argv[1])=="--captured-kinds"){CapturedKinds(false);CapturedKinds(true);return 0;}
  if(argc==2&&std::string_view(argv[1])=="--projected-lifetime") {
    for(unsigned form=0;form<4;++form)for(unsigned copies:{0U,1U,200U,201U,400U,800U})for(bool split:{false,true})ScopeExitSource(form,copies,split,true,true);
    return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--scope-exits") {
    for(unsigned form=0;form<4;++form)for(unsigned copies:{0U,1U,200U,201U})for(bool split:{false,true})ScopeExitSource(form,copies,split,true);
    return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--activation") {
    for(unsigned copies:{0U,1U,200U,201U})for(bool split:{false,true})ActivationSource(copies,split,true);
    return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--source") {
    for(unsigned form=0;form<4;++form)for(bool reverse:{false,true})CompletionTypes(form,reverse,true);
    return 0;
  }
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
  for(unsigned form=0;form<4;++form){ScopeExitSource(form,0,false,false);ScopeExitSource(form,0,false,false,true);}
  CapturedKinds(false);CapturedKinds(true);ActivationKinds(false);ActivationKinds(true);
  ScopedExposedKinds(false);ScopedExposedKinds(true);
  ActivationSource(0,false,false);ActivationSource(1,true,false);
  Case(false);Case(true);ChangedDefinition(false);ChangedDefinition(true);
  OriginalEntries(false);OriginalEntries(true);
  for(unsigned seed=0;seed<512;++seed)for(bool reverse:{false,true})KindOracle(seed,reverse);
  for(unsigned form=0;form<4;++form)for(bool reverse:{false,true})CompletionTypes(form,reverse,false);
  std::uint64_t work=0;std::size_t bytes=0;
  std::pair<std::uint64_t,std::size_t> previous;
  for(auto count:{200u,201u,400u,800u}) {
    const auto candidate=CandidateDeclarations(true,count);
    if(previous.first){Check(candidate.first<previous.first*3);Check(candidate.second<previous.second*3);}previous=candidate;
    Fixture f(count,true);const auto result=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input()));
    if(work){Check(result.work<work*3);Check(result.allocated_bytes<bytes*3);}work=result.work;bytes=result.allocated_bytes;
    std::cout<<count<<" expressions="<<f.tree.expressions.size()<<" events="<<result.flow.events.size()<<" work="<<work<<" bytes="<<bytes<<" candidate_work="<<candidate.first<<" candidate_bytes="<<candidate.second<<'\n';
  }
}
