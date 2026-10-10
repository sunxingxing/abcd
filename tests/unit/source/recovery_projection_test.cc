#include "../../../src/source/recovery_projection.h"
#include "../../../src/source/snapshot_projection.h"
#include "../../../src/source/flow_kinds.h"
#include "../../../src/source/cell_access.h"
#include "../../../src/source/class_tree.h"
#include "../../../src/source/loop_syntax.h"
#include "../../../src/source/nullish_syntax.h"
#include "../../../src/source/family_print.h"
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
// The complete per-event equations remain a small-graph oracle for sparse
// transport. Compare semantic products, never their allocation/layout details.
std::variant<FlowNormalKinds,TreeStructureError> CheckedFlowNormalKinds(
    const FunctionTree& tree,const RecoveryFlow& flow,const FlowLiveness& live,
    const RecoveryExpressionInput& input,analysis::SemanticBudget allowance={},
    const FlowEffectInput* completion=nullptr,const FlowInitializationProgram* initialization=nullptr) {
  auto dense=AnalyzeFlowNormalKinds(tree,flow,live,input,allowance,completion,initialization);
  if(const auto* expected=std::get_if<FlowNormalKinds>(&dense)) {
    auto prepared=BuildFlowValueStates(flow);Check(std::holds_alternative<FlowValueStates>(prepared));
    const auto sparse=AnalyzeFlowNormalKinds(tree,flow,std::get<FlowValueStates>(prepared),input,{},completion,initialization);
    Check(std::holds_alternative<FlowNormalKinds>(sparse));const auto& actual=std::get<FlowNormalKinds>(sparse);
    Check(actual.expressions==expected->expressions);Check(actual.truths==expected->truths);Check(actual.executable==expected->executable);
    Check(actual.initialized_reads==expected->initialized_reads);Check(actual.read_completions==expected->read_completions);
    Check(actual.original_effects==expected->original_effects);
    Check(actual.captured_inputs==expected->captured_inputs);
  }
  return dense;
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
    original.statements=statements;original.bindings=bindings;original.input_ranges.resize(expressions);
    // Synthetic analysis input: a trial is allowed to ask about an unproved
    // replacement. Its old operand correspondence is not proof for the new value.
    for(auto read:reads)InputEvidence(read);
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
      for(auto& input:original.inputs)input.expression=map(input.expression);
      std::reverse(original.input_ranges.begin(),original.input_ranges.end());
    }
  }
  // Raw input correspondence carries no result or projected-value proof.
  // These synthetic finite inputs must not type the current source occurrence.
  void InputEvidence(ExprId expression) {
    original.input_ranges[expression.value]={original.inputs.size(),1};
    original.inputs.push_back({expression,analysis::IntegerLiteral{7},analysis::IntegerLiteral{7},{}});
  }
  RecoveryExpressionInput Input() const {return {plan,equations,order,&original};}
  LegacyFlowProjection Projection() const {
    LegacyFlowProjection result{plan.aliases,plan.literals,plan.replacements,plan.omitted,{},{},plan.statements,private_bindings};
    result.storage=plan.storage;result.read_literals=plan.read_literals;result.transports=plan.transports;
    result.read_bindings=plan.read_bindings;result.read_sources=plan.read_sources;result.original=&original;return result;
  }
};
// Independent source programs exercise control/value feedback. Their domains
// are checked against literal language outcomes, then emitted source is run
// against separately written JS, never against this graph or its facts.
std::pair<std::uint64_t,std::size_t> ControlCompletions(unsigned form,bool reverse,bool split,
    unsigned copies,bool original,bool emit=false) {
  Fixture f(0,false);auto& tree=f.tree;
  const auto expr=[&](TreeExpression node,FlowValueEquation equation=UnknownFlowValue{}) {
    ExprId id{static_cast<unsigned>(tree.expressions.size())};tree.expressions.push_back({std::move(node),{0}});
    f.equations.push_back(equation);f.order.push_back(id);return id;
  };
  const auto stmt=[&](TreeStatement node) {
    StmtId id{static_cast<unsigned>(tree.statements.size())};tree.statements.push_back({std::move(node),{0}});return id;
  };
  const auto scope=[&](ScopeId parent) {
    ScopeId id{static_cast<unsigned>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kBlock,parent});return id;
  };
  const auto block=[&](ScopeId at,std::vector<StmtId> children) {
    const auto first=tree.children.size();tree.children.insert(tree.children.end(),children.begin(),children.end());
    return stmt(BlockStmt{at,static_cast<unsigned>(first),static_cast<unsigned>(children.size())});
  };
  const auto read=[&](SymbolId binding){return expr(SymbolExpr{binding},FlowBindingValue{binding});};
  const auto call=expr(CallExpr{read({0}),{0,0},CallReference::kValue});
  if(form<=3){tree.names.push_back(u"saved");tree.symbols.push_back({{1},{1},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});}
  std::vector<StmtId> body;
  const auto constant=expr(TreeLiteral{form==8?TreeLiteral{NullLiteral{}}:TreeLiteral{BooleanLiteral{form==2||form==3||form==4||form==7}}});
  ExprId control=constant;
  for(unsigned i=0;i<copies;++i) {
    const NameId name{static_cast<unsigned>(tree.names.size())};const auto suffix=std::to_string(i);
    tree.names.push_back(u"copy"+std::u16string(suffix.begin(),suffix.end()));
    const SymbolId symbol{static_cast<unsigned>(tree.symbols.size())};
    tree.symbols.push_back({{1},name,BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
    body.push_back(stmt(DeclarationStmt{symbol,control}));control=read(symbol);
  }
  ExprId result{};
  if(form<=2) {
    body.push_back(stmt(DeclarationStmt{{1},{}}));
    const auto assign=[&](ExprId value){return stmt(ExpressionStmt{expr(AssignmentExpr{SymbolTarget{{1}},value},FlowExpressionValue{value})});};
    const auto yes=scope({1});
    auto consequent=block(yes,{assign(call)});
    if(split){const auto outer=scope({1});tree.scopes[yes.value].parent=outer;consequent=block(outer,{consequent});}
    const auto alternate=form==1?std::optional<StmtId>{}:block(scope({1}),{assign(expr(TreeLiteral{IntegerLiteral{7}}))});
    body.push_back(stmt(IfStmt{control,consequent,alternate}));result=read({1});
  } else if(form==3) {
    body.push_back(stmt(DeclarationStmt{{1},control}));
    const auto loop=stmt(LoopStmt{});const auto at=scope({1});
    control=expr(UnaryExpr{TreeUnary::kNot,read({1})});
    const auto stop=block(scope(at),{stmt(BreakStmt{loop,{}})});
    const auto value=expr(TreeLiteral{BooleanLiteral{false}});
    const auto write=stmt(ExpressionStmt{expr(AssignmentExpr{SymbolTarget{{1}},value},FlowExpressionValue{value})});
    auto loop_body=block(at,{stmt(IfStmt{control,stop,{}}),write,stmt(ExpressionStmt{call})});
    if(split){const auto outer=scope({1});tree.scopes[at.value].parent=outer;loop_body=block(outer,{loop_body});}
    std::get<LoopStmt>(tree.statements[loop.value].statement).body=loop_body;body.push_back(loop);result=read({1});
  } else if(form<=5) {
    const auto seven=expr(TreeLiteral{IntegerLiteral{7}});
    result=expr(ConditionalExpr{control,seven,call},FlowChoiceValue{control,seven,call});
  } else {
    const auto operation=form==6?TreeLogical::kAnd:form==7?TreeLogical::kOr:TreeLogical::kNullish;
    result=expr(LogicalExpr{operation,control,call},FlowLogicalValue{operation,control,call});
  }
  body.push_back(stmt(ReturnStmt{result}));
  const auto first=tree.children.size();tree.children.insert(tree.children.end(),body.begin(),body.end());
  std::get<BlockStmt>(tree.statements[0].statement)={{1},static_cast<unsigned>(first),static_cast<unsigned>(body.size())};
  const auto x=tree.expressions.size(),b=tree.symbols.size(),s=tree.statements.size();
  f.plan.aliases.resize(b);f.plan.storage.resize(b);f.plan.read_bindings.resize(b);f.plan.literals.resize(b);
  f.plan.read_sources.resize(x);f.plan.read_literals.resize(x);f.plan.replacements.resize(x);f.plan.transports.resize(x);
  f.plan.omitted.resize(s);f.plan.statements.resize(s);f.private_bindings.assign(b,1);f.private_bindings[0]=0;
  f.original.bindings=b;f.original.statements=s;f.original.input_ranges.resize(x);
  ExprId called=call;
  if(reverse) {
    const auto map=[&](ExprId id){return ExprId{static_cast<unsigned>(x-1-id.value)};};
    for(auto& item:tree.expressions)std::visit([&](auto& value) {
      using T=std::decay_t<decltype(value)>;
      if constexpr(std::is_same_v<T,CallExpr>)value.callee=map(value.callee);
      else if constexpr(std::is_same_v<T,AssignmentExpr>)value.value=map(value.value);
      else if constexpr(std::is_same_v<T,UnaryExpr>)value.operand=map(value.operand);
      else if constexpr(std::is_same_v<T,ConditionalExpr>){value.condition=map(value.condition);value.consequent=map(value.consequent);value.alternate=map(value.alternate);}
      else if constexpr(std::is_same_v<T,LogicalExpr>){value.left=map(value.left);value.right=map(value.right);}
      else Check(std::is_same_v<T,SymbolExpr>||std::is_same_v<T,TreeLiteral>);
    },item.expression);
    for(auto& item:tree.statements)std::visit([&](auto& value) {
      using T=std::decay_t<decltype(value)>;
      if constexpr(std::is_same_v<T,DeclarationStmt>){if(value.initializer)value.initializer=map(*value.initializer);}
      else if constexpr(std::is_same_v<T,ExpressionStmt>)value.expression=map(value.expression);
      else if constexpr(std::is_same_v<T,IfStmt>)value.condition=map(value.condition);
      else if constexpr(std::is_same_v<T,ReturnStmt>){if(value.value)value.value=map(*value.value);}
    },item.statement);
    for(auto& equation:f.equations)std::visit([&](auto& value) {
      using T=std::decay_t<decltype(value)>;
      if constexpr(std::is_same_v<T,FlowExpressionValue>)value.expression=map(value.expression);
      else if constexpr(std::is_same_v<T,FlowChoiceValue>){value.condition=map(value.condition);value.consequent=map(value.consequent);value.alternate=map(value.alternate);}
      else if constexpr(std::is_same_v<T,FlowLogicalValue>){value.left=map(value.left);value.right=map(value.right);}
    },equation);
    for(auto& id:f.order)id=map(id);
    std::reverse(tree.expressions.begin(),tree.expressions.end());std::reverse(f.equations.begin(),f.equations.end());
    control=map(control);called=map(called);result=map(result);
  }
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  auto projection=f.Projection();auto input=f.Input();if(!original){projection.original=nullptr;input.original=nullptr;}
  const auto prepared=PrepareRecoveryProjection(tree,projection,input);
  Check(std::holds_alternative<RecoveryProjection>(prepared));const auto& current=std::get<RecoveryProjection>(prepared);
  const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(current.flow));
  const auto calls=[&](const RecoveryFlow& flow,const FlowLiveness& states) {
    unsigned reachable=0;for(unsigned i=0;i<flow.events.size();++i)
      if(states.reachable[i]&&flow.events[i].kind==FlowEventKind::kEvaluation&&flow.events[i].expression==called)++reachable;
    return reachable;
  };
  Check(calls(current.flow,live)==(form==2||form==3||form==5||form==8?1U:0U));
  for(const auto& event:current.flow.events)if(event.kind==FlowEventKind::kTruthBranch&&event.expression==control) {
    unsigned arms=0;for(auto edge=event.outgoing;edge;edge=current.flow.edges[*edge].outgoing)++arms;
    Check(arms==(form==3?2U:1U));
  }
  const auto truth=current.flow.normal_truths[result.value];
  if(form==0||form==4) {
    if(!current.expressions.kinds[result.value].IsNumber()||truth!=TruthDomains::Truthy())
      std::cerr<<"control result form="<<form<<" reverse="<<reverse<<" copies="<<copies
          <<" number="<<current.expressions.kinds[result.value].IsNumber()<<" truth="<<(truth==TruthDomains::Truthy())<<'\n';
    Check(current.expressions.kinds[result.value].IsNumber()&&truth==TruthDomains::Truthy());
  }
  if(form==1)Check(truth==TruthDomains::One(TruthDomain::kUndefined));
  if(form==3||form==6||form==9)Check(truth==TruthDomains::One(TruthDomain::kFalsy)||form==3&&truth==(TruthDomains::Truthy()|TruthDomains::One(TruthDomain::kFalsy)));
  if(form==7)Check(truth==TruthDomains::Truthy());
  if(form==3)Check(live.reachable[current.flow.terminal.value]);
  if(copies==0) {
    const auto checked=std::get<FlowNormalKinds>(CheckedFlowNormalKinds(tree,current.flow,live,input));
    Check(checked.expressions[result.value]==current.expressions.kinds[result.value]);Check(checked.truths[result.value]==truth);
  }
  if(copies==0&&form==0) {
    // Same occurrence IDs, changed proposal: both control and value facts
    // must rebuild. Reusing the committed executable mask loses the call.
    auto candidate=f.plan;candidate.primitives.push_back(BooleanLiteral{true});
    candidate.transports[control.value]=ExpressionTransport{control,PrimitiveId{static_cast<unsigned>(candidate.primitives.size()-1)}};
    const RecoveryExpressionInput changed{candidate,f.equations,f.order,input.original};
    const auto trial=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,projection,changed));
    Check(calls(trial.flow,std::get<FlowLiveness>(AnalyzeFlowLiveness(trial.flow)))==1);
    Check(!trial.expressions.kinds[result.value].IsNumber());
    Check(calls(current.flow,live)==0);
    const analysis::SemanticBudget exact{current.allocated_bytes,current.work};
    Check(std::holds_alternative<RecoveryProjection>(PrepareRecoveryProjection(tree,projection,input,exact)));
    Check(std::get<TreeStructureError>(PrepareRecoveryProjection(tree,projection,input,{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PrepareRecoveryProjection(tree,projection,input,{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
  }
  if(emit) {
    std::ostringstream output;const auto printed=PrintFunctionTree(tree,output);
    Check(std::holds_alternative<PrintedFunction>(printed));Check(!std::get<PrintedFunction>(printed).incomplete);
    std::cout<<nlohmann::json{{"form",form},{"copies",copies},{"reverse",reverse},{"split",split},{"original",original},
        {"source",output.str()},{"expressions",x},{"edges",current.flow.edges.size()},{"work",current.work},{"bytes",current.allocated_bytes}}<<'\n';
  }
  return {current.work,current.allocated_bytes};
}
// Synthetic path-range certificates exercise the shared consumer. Decoded
// FunctionTree tests separately prove publication of the control/edge owner.
std::pair<std::uint64_t,std::size_t> OriginalControlKinds(bool reverse,unsigned count=1,unsigned copies=0) {
  using namespace analysis;
  Fixture f(0,false);auto& tree=f.tree;
  const auto expr=[&](TreeExpression value,FlowValueEquation equation=UnknownFlowValue{}) {
    ExprId id{static_cast<unsigned>(tree.expressions.size())};tree.expressions.push_back({value,{0}});
    f.equations.push_back(equation);f.order.push_back(id);return id;
  };
  const auto stmt=[&](TreeStatement value) {
    StmtId id{static_cast<unsigned>(tree.statements.size())};tree.statements.push_back({value,{0}});return id;
  };
  const auto block=[&](ScopeId scope,StmtId child) {
    const auto first=tree.children.size();tree.children.push_back(child);
    return stmt(BlockStmt{scope,static_cast<unsigned>(first),1});
  };
  tree.names.push_back(u"saved");tree.symbols.push_back({{1},{1},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kPhiJoin});
  const auto declared=stmt(DeclarationStmt{{1},{}});
  f.original.values.push_back({EntryDefinition{ParameterEntry{0}},ValueKinds::Language()});
  std::vector<StmtId> body{declared};std::vector<ExprId> conditions,stores,reads;
  for(unsigned i=0;i<count;++i) {
    const ScopeId yes{static_cast<unsigned>(tree.scopes.size())},no{yes.value+1};
    tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
    auto condition=expr(BinaryExpr{BinaryOperator::kStrictEqual,expr(SymbolExpr{{0}},FlowBindingValue{{0}}),expr(TreeLiteral{source::IntegerLiteral{0}})});
    for(unsigned copy=0;copy<copies;++copy) {
      const SymbolId binding{static_cast<unsigned>(tree.symbols.size())};const NameId name{static_cast<unsigned>(tree.names.size())};
      const auto suffix=std::to_string(binding.value);tree.names.push_back(u"condition"+std::u16string(suffix.begin(),suffix.end()));
      tree.symbols.push_back({{1},name,BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
      const auto declaration=stmt(DeclarationStmt{binding,condition});body.push_back(declaration);
      // A layout variation retains its SSA copy correspondence. Omitting it
      // is a different, explicitly unproved input rather than queue-order evidence.
      const OpId operation{static_cast<unsigned>(f.original.operations.size())};
      const ValueId value{static_cast<unsigned>(f.original.values.size())};
      f.original.values.push_back({OpResult{operation},ValueKinds::Boolean()});
      OriginalOperation save{};save.life=ValueLife::kLive;save.result=value;save.expression=save.result_expression=condition;
      f.original.operations.push_back(save);f.original.declarations.resize(tree.statements.size());
      f.original.declarations[declaration.value]=OriginalSourceDefinition{OriginalResultDefinition{operation,value},binding,condition};
      condition=expr(SymbolExpr{binding},FlowBindingValue{binding});
    }
    const auto number=expr(UnaryExpr{TreeUnary::kPlus,expr(SymbolExpr{{0}},FlowBindingValue{{0}})});
    const auto first=expr(AssignmentExpr{SymbolTarget{{1}},number},FlowExpressionValue{number});
    const auto seven=expr(TreeLiteral{source::IntegerLiteral{7}},FlowPrimitiveReference{{1}});
    const auto second=expr(AssignmentExpr{SymbolTarget{{1}},seven},FlowExpressionValue{seven});
    body.push_back(stmt(IfStmt{condition,block(yes,stmt(ExpressionStmt{first})),block(no,stmt(ExpressionStmt{second}))}));
    const auto read=expr(SymbolExpr{{1}},FlowBindingValue{{1}});
    body.push_back(stmt(ExpressionStmt{expr(UnaryExpr{TreeUnary::kNegate,read})}));
    conditions.push_back(condition);stores.push_back(first);reads.push_back(read);
    const EdgeId edge{static_cast<unsigned>(f.original.edges.size())};
    f.original.edges.push_back({{i*3},{i*3+1},FlowEdgeKind::kTaken,0});
    f.original.edges.push_back({{i*3},{i*3+2},FlowEdgeKind::kFallthrough,1});
    const OpId branch_id{static_cast<unsigned>(f.original.operations.size())};
    f.original.edge_controls.insert(f.original.edge_controls.end(),{branch_id,branch_id});
    OriginalOperation branch{};branch.life=ValueLife::kLive;
    branch.control=OriginalControlInput{condition,OriginalControlKind::kPredicate,
        PureTruthTest{TruthDomains::All(),TruthDomains::Truthy(),PureTruthOutcome::kDecision}};
    f.original.operations.push_back(branch);
    const ValueId value{static_cast<unsigned>(f.original.values.size())};
    f.original.values.push_back({PhiDefinition{{i*3+3}},ValueKinds::FiniteNumber()});
    f.original.definitions.resize(tree.expressions.size());
    f.original.definitions[first.value]=OriginalSourceDefinition{OriginalPhiDefinition{value,edge},{1},number};
    f.original.definitions[second.value]=OriginalSourceDefinition{OriginalPhiDefinition{value,EdgeId{edge.value+1}},{1},seven};
  }
  const auto root=tree.children.size();tree.children.insert(tree.children.end(),body.begin(),body.end());
  std::get<BlockStmt>(tree.statements[0].statement)={{1},static_cast<unsigned>(root),static_cast<unsigned>(body.size())};
  const auto x=tree.expressions.size(),s=tree.statements.size(),b=tree.symbols.size();
  f.plan.aliases.resize(b);f.plan.storage.resize(b);f.plan.read_bindings.resize(b);f.plan.literals.resize(b);
  f.plan.read_sources.resize(x);f.plan.read_literals.resize(x);f.plan.replacements.resize(x);f.plan.transports.resize(x);
  f.plan.omitted.resize(s);f.plan.statements.resize(s);f.private_bindings.assign(b,1);f.private_bindings[0]=0;
  f.original.statements=s;f.original.bindings=b;f.original.input_ranges.resize(x);f.original.definitions.resize(x);
  if(copies)f.original.declarations.resize(s);
  if(reverse) {
    const auto map=[&](ExprId id){return ExprId{static_cast<unsigned>(x-1-id.value)};};
    for(auto& node:tree.expressions)std::visit([&](auto& value) {
      using T=std::decay_t<decltype(value)>;
      if constexpr(std::is_same_v<T,UnaryExpr>)value.operand=map(value.operand);
      else if constexpr(std::is_same_v<T,BinaryExpr>){value.left=map(value.left);value.right=map(value.right);}
      else if constexpr(std::is_same_v<T,AssignmentExpr>)value.value=map(value.value);
      else Check(std::is_same_v<T,SymbolExpr>||std::is_same_v<T,TreeLiteral>);
    },node.expression);
    for(auto& node:tree.statements)std::visit([&](auto& value) {
      using T=std::decay_t<decltype(value)>;
      if constexpr(std::is_same_v<T,IfStmt>)value.condition=map(value.condition);
      else if constexpr(std::is_same_v<T,ExpressionStmt>)value.expression=map(value.expression);
      else if constexpr(std::is_same_v<T,DeclarationStmt>){if(value.initializer)value.initializer=map(*value.initializer);}
    },node.statement);
    for(auto& value:f.equations)if(auto* expression=std::get_if<FlowExpressionValue>(&value))expression->expression=map(expression->expression);
    for(auto& value:f.order)value=map(value);
    for(auto& definition:f.original.definitions)if(definition)definition->input=map(definition->input);
    for(auto& definition:f.original.declarations)if(definition)definition->input=map(definition->input);
    std::reverse(tree.expressions.begin(),tree.expressions.end());std::reverse(f.equations.begin(),f.equations.end());
    std::reverse(f.original.definitions.begin(),f.original.definitions.end());
    for(auto& id:conditions)id=map(id);for(auto& id:stores)id=map(id);for(auto& id:reads)id=map(id);
    for(auto& operation:f.original.operations) {
      if(operation.control)operation.control->expression=map(operation.control->expression);
      if(operation.expression)operation.expression=map(*operation.expression);
      if(operation.result_expression)operation.result_expression=map(*operation.result_expression);
    }
  }
  const auto condition=conditions.front(),first=stores.front(),read=reads.front();
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  auto projection=f.Projection();std::vector<std::uint8_t> stable(b);stable[0]=1;projection.stable_entries=stable;
  const auto model=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,projection,f.Input()));
  Check(model.flow.original_controls.size()==count*(1+copies));
  for(const auto at:reads)Check(model.expressions.kinds[at.value].IsFiniteNumber());
  unsigned native_initializers=0;
  for(const auto& definition:model.flow.definitions)
    if(definition.kind==FlowDefinitionKind::kInitializer&&definition.declaration) {
      Check(definition.original==&*f.original.declarations[definition.declaration->value]);++native_initializers;
    }
  Check(native_initializers==count*copies);
  if(count==1) {
    const SemanticBudget exact{model.allocated_bytes,model.work};
    Check(std::holds_alternative<RecoveryProjection>(PrepareRecoveryProjection(tree,projection,f.Input(),exact)));
    Check(std::get<TreeStructureError>(PrepareRecoveryProjection(tree,projection,f.Input(),{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PrepareRecoveryProjection(tree,projection,f.Input(),{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
  }
  auto gap=model.flow;gap.original_controls.clear();
  const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(gap));
  const auto unknown=std::get<FlowNormalKinds>(CheckedFlowNormalKinds(tree,gap,live,f.Input()));
  Check(unknown.expressions[read.value].IsNumber()&&!unknown.expressions[read.value].IsFiniteNumber());
  Check(OriginalDefinitionKinds(gap,*f.original.definitions[first.value]).IsEmpty());
  const auto phi=std::get<OriginalPhiDefinition>(f.original.definitions[first.value]->origin).value;
  const OriginalSourceDefinition saved{OriginalSavedDefinition{phi,IncomingKey{EdgeId{0}}},{1},f.original.definitions[first.value]->input};
  Check(OriginalDefinitionKinds(model.flow,saved).IsFiniteNumber());Check(OriginalDefinitionKinds(gap,saved).IsEmpty());
  if(count==1) {
    auto truths=model.flow.normal_truths;truths[condition.value]=TruthDomains::One(TruthDomain::kFalsy);
    Check(OriginalDefinitionControl(model.flow,saved)==analysis::OpId{copies});
    Check(OriginalDefinitionKinds(model.flow,saved,truths).IsEmpty());
    Check(OriginalDefinitionKinds(model.flow,*f.original.definitions[first.value],truths).IsEmpty());
    Check(OriginalDefinitionProof(model.flow,saved,truths).state==FlowOriginalProofState::kUnreachable);
    Check(OriginalDefinitionProof(gap,saved,truths).state==FlowOriginalProofState::kUnavailable);
    truths[condition.value]={};
    Check(OriginalDefinitionProof(model.flow,saved,truths).state==FlowOriginalProofState::kPending);
    Check(OriginalDefinitionProof(model.flow,*f.original.definitions[first.value],truths).state==FlowOriginalProofState::kPending);
    truths[condition.value]=TruthDomains::Truthy();
    Check(OriginalDefinitionKinds(model.flow,saved,truths).IsFiniteNumber());
    Check(OriginalDefinitionProof(model.flow,saved,truths).state==FlowOriginalProofState::kAvailable);
    const auto* branch=std::get_if<IfStmt>(&tree.statements[body[1+copies].value].statement);Check(branch&&branch->alternate);
    const auto& block=std::get<BlockStmt>(tree.statements[branch->alternate->value].statement);
    const auto second=std::get<ExpressionStmt>(tree.statements[tree.children[block.first_child].value].statement).expression;
    Check(OriginalDefinitionKinds(model.flow,*f.original.definitions[second.value],truths).IsEmpty());
    const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(model.flow));
    const auto cold=std::get<FlowNormalKinds>(CheckedFlowNormalKinds(tree,model.flow,live,f.Input()));
    Check(cold.expressions[read.value].IsFiniteNumber());
  }
  const auto input=copies?*std::get<DeclarationStmt>(tree.statements[body[1].value].statement).initializer:condition;
  for(const auto substituted:{condition,input}) {
    auto candidate=f.plan;candidate.primitives.push_back(BooleanLiteral{true});
    candidate.transports[substituted.value]=ExpressionTransport{substituted,PrimitiveId{static_cast<unsigned>(candidate.primitives.size()-1)}};
    const RecoveryExpressionInput changed{candidate,f.equations,f.order,&f.original};
    const auto current=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,projection,changed));
    Check(current.expressions.kinds[read.value].IsNumber()&&!current.expressions.kinds[read.value].IsFiniteNumber());
  }
  auto no_raw=projection;no_raw.original=nullptr;
  no_raw.evaluations=model.expressions.evaluations;no_raw.normal_stores=model.expressions.normal_stores;
  no_raw.normal_bindings=model.expressions.bindings;no_raw.normal_kinds=model.expressions.kinds;
  const auto plain=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,no_raw,{},model.flow));
  Check(plain.original_controls.empty()&&plain.original_controls.capacity()==0);
  Check(plain.normal_truths.empty()&&plain.normal_truths.capacity()==0);
  for(const auto& definition:plain.definitions)Check(definition.original==nullptr);
  return {model.work,model.allocated_bytes};
}
// Conditional effect certificates are synthetic inputs to the solver here;
// decoded FunctionTree tests independently verify their publication boundary.
void OriginalIntrinsicEffects(bool reverse) {
  using L=analysis::LanguageEffect;using X=analysis::ExceptionalCompletion;
  Fixture f(1,reverse);
  f.original.values.push_back({analysis::EntryDefinition{analysis::ParameterEntry{0}},analysis::ValueKinds::Language(),{}});
  f.original.values.push_back({analysis::OpResult{{0}},analysis::ValueKinds::Number(),{}});
  const auto expression=f.values[0];
  const auto operand=std::get<UnaryExpr>(f.tree.expressions[expression.value].expression).operand;
  const auto callee=std::get<CallExpr>(f.tree.expressions[operand.value].expression).callee;
  f.original.operations.push_back({{0},{0},0,analysis::ValueId{1},L::kNone,L::kNone,X::kImpossible,
      analysis::ValueLife::kLive,{0},expression,{},expression,expression});
  auto projection=f.Projection();std::vector<std::uint8_t> stable(f.tree.symbols.size(),0);stable[0]=1;projection.stable_entries=stable;
  const auto effects=[&](const RecoveryProjection& model,ExprId id) {
    for(const auto& event:model.flow.events)if(event.kind==FlowEventKind::kEvaluation&&event.expression==id) {
      Check(event.effects.has_value());return *event.effects;
    }
    Check(false);return SourceEffects{};
  };
  const auto current=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,projection,f.Input()));
  Check(effects(current,expression).Evaluation()==ExpressionEvaluation::kInert);
  Check(HasLanguageEffect(effects(current,operand).normal,L::kUserCode));
  // The executable-completion solve itself consumes the proof, before final
  // edge compaction. A final-only refinement would publish stale joint facts.
  auto conservative=current.flow;
  std::optional<FlowPoint> action;
  for(unsigned i=0;i<conservative.events.size();++i) {
    auto& event=conservative.events[i];event.original_effect_valid=false;
    if(event.kind==FlowEventKind::kEvaluation&&event.expression==expression)action=FlowPoint{i};
  }
  Check(action.has_value());const auto edge=conservative.edges.size();
  conservative.edges.push_back({*action,conservative.terminal,FlowCompletion::kThrow,
      conservative.events[conservative.terminal.value].incoming,conservative.events[action->value].outgoing});
  conservative.events[conservative.terminal.value].incoming=edge;conservative.events[action->value].outgoing=edge;
  const auto states=std::get<FlowValueStates>(BuildFlowValueStates(conservative));
  const FlowEffectInput completion{current.expressions.evaluations,current.expressions.normal_stores,current.expressions.evaluations,{}};
  const auto solved=std::get<FlowNormalKinds>(AnalyzeFlowNormalKinds(f.tree,conservative,states,f.Input(),{},&completion));
  Check(!solved.executable[edge]);

  // Result-value identity alone does not establish an intrinsic-effect bound.
  f.original.operations[0].intrinsic_expression.reset();
  const auto value_only=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,projection,f.Input()));
  Check(effects(value_only,expression).exceptional==X::kMayThrow);
  Check(HasLanguageEffect(effects(value_only,expression).normal,L::kUserCode));
  f.original.operations[0].intrinsic_expression=expression;
  // Retaining the same source node/operation IDs cannot retain a certificate
  // after a projected operand edit. Unknown entry identity also withdraws it.
  f.plan.read_sources[callee.value]=SymbolId{0};
  const auto changed=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,projection,f.Input()));
  Check(effects(changed,expression).exceptional==X::kMayThrow);
  Check(HasLanguageEffect(effects(changed,expression).normal,L::kUserCode));
  f.plan.read_sources[callee.value].reset();stable[0]=0;
  const auto unknown=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,projection,f.Input()));
  Check(HasLanguageEffect(effects(unknown,expression).normal,L::kUserCode));
  // Normal and exceptional callbacks are independent, and UserCode denotes
  // transitive effects rather than an isolated bit eligible for intersection.
  const SourceEffects callback=CallbackEffects();
  const auto split=IntersectSourceEffects(callback,{L::kNone,L::kUserCode,X::kMayThrow});
  Check(split.normal==L::kNone&&HasLanguageEffect(split.before_throw,L::kUserCode)&&split.exceptional==X::kMayThrow);
  const auto guard=IntersectSourceEffects({L::kGuard,L::kGuard,X::kMayThrow},callback);
  Check(guard.normal==L::kGuard&&guard.before_throw==L::kGuard&&guard.exceptional==X::kMayThrow);
  // An update's extra lexical GetValue is not its conversion action. Even a
  // valid intrinsic no-throw certificate cannot erase an unproved TDZ read.
  auto update_tree=f.tree;
  update_tree.expressions[expression.value].expression=UpdateExpr{SymbolId{1},UpdateDirection::kIncrement,UpdateResult::kPrefix};
  FlowEvent read;read.kind=FlowEventKind::kRead;read.expression=expression;read.original_effect_valid=true;
  const FlowEffectInput facts{current.expressions.evaluations,current.expressions.normal_stores,current.expressions.evaluations,current.expressions.kinds};
  const auto read_effect=EvaluateFlowEventEffects(current.flow,update_tree,read,facts);
  Check(read_effect.intrinsic&&read_effect.intrinsic->exceptional==X::kMayThrow);
  Check(read_effect.retention==ExpressionEvaluation::kRequired);
}

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
  f.original.input_ranges.resize(expression_count);
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
    f.original.input_ranges.resize(expressions);
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
std::pair<std::uint64_t,std::size_t> JointProjectionSource(bool reverse,unsigned count,bool before,bool emit) {
  FunctionTree tree{};tree.callable=CallableKind::kFunction;tree.strictness=Strictness::kStrict;
  tree.parameter_scope={0};tree.body_scope={1};tree.body={0};tree.origins={OriginalSite{{0},{0}}};
  tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
  tree.names={u"input",u"report",u"saved"};
  for(unsigned i=0;i<2;++i){tree.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}});tree.parameters.push_back({{i},{},false});}
  tree.symbols.push_back({{1},{2},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
  tree.statements.push_back({BlockStmt{{1},0,0},{0}});
  const auto expr=[&](TreeExpression node){ExprId id{static_cast<unsigned>(tree.expressions.size())};tree.expressions.push_back({std::move(node),{0}});return id;};
  const auto read=[&](unsigned symbol){return expr(SymbolExpr{{symbol}});};
  const auto call=[&](unsigned symbol,std::vector<ExprId> arguments){const auto first=tree.arguments.size();tree.arguments.insert(tree.arguments.end(),arguments.begin(),arguments.end());return expr(CallExpr{read(symbol),{static_cast<unsigned>(first),static_cast<unsigned>(arguments.size())},CallReference::kValue});};
  const auto stmt=[&](TreeStatement node){StmtId id{static_cast<unsigned>(tree.statements.size())};tree.statements.push_back({std::move(node),{0}});return id;};
  const auto declaration=stmt(DeclarationStmt{{2},call(0,{})});
  std::vector<StmtId> body;
  if(!before)body.push_back(declaration);
  std::vector<ExprId> sequences;
  for(unsigned i=0;i<count;++i) {
    const auto sequence=expr(SequenceExpr{read(2),call(0,{})});sequences.push_back(sequence);
    body.push_back(stmt(ExpressionStmt{call(1,{sequence})}));
  }
  // The discarded-right case used to request flow after borrowing provisional
  // expression facts. Mix it with value-used sequences in arbitrary arena order.
  const auto tail=expr(SequenceExpr{call(1,{read(2)}),expr(TreeLiteral{IntegerLiteral{7}})});
  sequences.push_back(tail);body.push_back(stmt(ExpressionStmt{tail}));
  if(before)body.push_back(declaration);
  body.push_back(stmt(ReturnStmt{read(2)}));tree.children=body;
  std::get<BlockStmt>(tree.statements[0].statement).child_count=body.size();
  if(reverse) {
    const auto map=[&](ExprId id){return ExprId{static_cast<unsigned>(tree.expressions.size()-1-id.value)};};
    for(auto& item:tree.expressions)std::visit([&](auto& node){using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,CallExpr>)node.callee=map(node.callee);
      else if constexpr(std::is_same_v<T,SequenceExpr>){node.left=map(node.left);node.right=map(node.right);}
    },item.expression);
    for(auto& item:tree.statements)std::visit([&](auto& node){using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,DeclarationStmt>){if(node.initializer)node.initializer=map(*node.initializer);}
      else if constexpr(std::is_same_v<T,ExpressionStmt>)node.expression=map(node.expression);
      else if constexpr(std::is_same_v<T,ReturnStmt>){if(node.value)node.value=map(*node.value);}
    },item.statement);
    for(auto& argument:tree.arguments)argument=map(argument);
    for(auto& sequence:sequences)sequence=map(sequence);
    std::reverse(tree.expressions.begin(),tree.expressions.end());
  }
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto plan=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  if(!before)for(const auto sequence:sequences)Check(plan.replacements[sequence.value].has_value());
  else if(count)Check(!plan.replacements[sequences[0].value]);
  if(count==1) {
    Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes,plan.work})));
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes-1,plan.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes,plan.work-1}))==TreeStructureError::kWorkLimit);
  }
  if(emit) {
    std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
    std::cout<<nlohmann::json{{"source",output.str()},{"count",count},{"reverse",reverse},{"before",before},{"work",plan.work},{"bytes",plan.allocated_bytes}}<<'\n';
  }
  return {plan.work,plan.allocated_bytes};
}
void Case(bool reverse) {
  Fixture f(1,reverse);
  TreeCheckBudget provisional_budget({});
  const auto provisional=AnalyzeRecoveryExpressions(f.tree,f.Input(),{},provisional_budget);
  Check(provisional.evaluations[f.reads[0].value]==ExpressionEvaluation::kRequired);
  const auto current=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input()));
  // The same proposal has distinct intermediate/final precision. Only the
  // completed owner is suitable for publication to an in-progress pass.
  Check(current.expressions.evaluations[f.reads[0].value]==ExpressionEvaluation::kInert);
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
  const auto source_only=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),raw));
  // Source ToNumber proves Number without raw IR; it cannot borrow the old
  // operand's finite range. Absence of correspondence selects no second solver.
  Check(source_only.expressions.kinds==source_only.flow.normal_kinds);
  Check(source_only.expressions.kinds[f.reads[0].value].IsNumber());
  Check(!source_only.expressions.kinds[f.reads[0].value].IsFiniteNumber());
  Check(source_only.expressions.evaluations[f.results[0].value]==ExpressionEvaluation::kInert);
}
void ChangedDefinition(bool reverse) {
  Fixture f(1,reverse);
  // The original successful operation produces Number; replacing the written
  // value with a parameter must recompute downstream domains despite retained raw input evidence.
  const auto call=std::get<UnaryExpr>(f.tree.expressions[f.values[0].value].expression).operand;
  const auto unknown=std::get<CallExpr>(f.tree.expressions[call.value].expression).callee;
  f.InputEvidence(f.values[0]);
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
  f.original.input_ranges.resize(size);
  // Retain finite raw inputs deliberately; projected reaching values must
  // determine the answer, independently of operand-only correspondence.
  for(const auto read:reads)if(read)f.InputEvidence(*read);
  const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(flow));
  const auto actual=std::get<FlowNormalKinds>(CheckedFlowNormalKinds(f.tree,flow,live,f.Input()));
  for(unsigned i=0;i<points;++i)if(reads[i])Check(actual.expressions[reads[i]->value]==kinds(incoming[i]));
  const analysis::SemanticBudget exact{actual.allocated_bytes,actual.work};
  Check(std::holds_alternative<FlowNormalKinds>(CheckedFlowNormalKinds(f.tree,flow,live,f.Input(),exact)));
  Check(std::get<TreeStructureError>(CheckedFlowNormalKinds(f.tree,flow,live,f.Input(),{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(CheckedFlowNormalKinds(f.tree,flow,live,f.Input(),{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
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
  f.original.bindings=bindings;f.original.statements=statements;f.original.input_ranges.resize(expressions);
  f.InputEvidence(after_read);
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
// Unit sites are allocation/transfer facts, never numeric domains or a fixed
// for-loop shape. Saved pre-reset values and current post-reset values differ.
// This manually specified completion graph has an independent scalar oracle;
// it is not a certificate for a decoded original loop/capture program.
std::pair<std::uint64_t,std::size_t> CurrentUnitKinds(bool reverse,bool loop,unsigned form,unsigned count=1) {
  Fixture f(count,reverse);RecoveryFlow flow{};flow.original=&f.original;
  const auto size=count*5;flow.events.resize(size);flow.bindings.resize(count+1);
  TreeCheckBudget scope_budget({});auto scopes=MakeFlowScopes(f.tree,scope_budget);
  const std::array<std::optional<FlowLoopScope>,1> units{FlowLoopScope{}};
  IndexFlowLoopScopes(*scopes,units,scope_budget);
  scopes->events.resize(size,{ScopeId{1},ScopeId{1}});
  scopes->loop_events.resize(size,{StmtId{0},StmtId{0}});flow.scopes=scopes;
  const auto point=[&](unsigned i){return FlowPoint{reverse?size-1-i:i};};flow.entry=point(0);
  for(unsigned n=0;n<count;++n) {
    const SymbolId binding{n+1};auto& cell=flow.bindings[binding.value];cell.kind=BindingKind::kLocal;
    cell.cell_access=(form==4||form==9)?CellVisibility::kExternal:CellVisibility::kClosed;
    cell.lifetime=loop?FlowBindingLifetime{LoopBindingLifetime{{0}}}:FlowBindingLifetime{ScopeBindingLifetime{{1},point(0),point(size-1)}};
    const auto first=point(n*5),input=point(n*5+1),boundary=point(n*5+2),commit=point(n*5+3),read=point(n*5+4);
    flow.events[first.value].definition=FlowDefinitionId{static_cast<unsigned>(flow.definitions.size())};
    flow.definitions.push_back({first,binding,FlowDefinitionKind::kInitializer,{},f.values[n]});
    flow.events[input.value].kind=FlowEventKind::kCopyInput;flow.events[input.value].read=binding;
    if(form<=2) {
      flow.events[boundary.value].kind=loop?FlowEventKind::kEnterIterationEnvironment:FlowEventKind::kEnterScope;
      if(loop)flow.events[boundary.value].loop_environment=StmtId{0};else flow.events[boundary.value].scope=ScopeId{1};
    }
    if(form==3||form==4||form==8||form==9)flow.events[boundary.value].effects=CallbackEffects();
    if(form!=2&&form!=8&&form!=9) {
      flow.events[commit.value].definition=FlowDefinitionId{static_cast<unsigned>(flow.definitions.size())};
      FlowDefinition carried{commit,binding,FlowDefinitionKind::kIterationCopy};
      carried.input=FlowCapturedValue{form==1?commit:input,binding};flow.definitions.push_back(carried);
    }
    if(form==5) {
      scopes->events[boundary.value]={UnmodeledScope{},UnmodeledScope{}};
      scopes->loop_events[boundary.value]={UnmodeledScope{},UnmodeledScope{}};
    }
    if(form==6) {
      scopes->events[read.value]={IncomingScope{},IncomingScope{}};
      scopes->loop_events[read.value]={ExitedLoopScope{{0}},ExitedLoopScope{{0}}};
    }
    flow.events[read.value].kind=FlowEventKind::kRead;flow.events[read.value].read=binding;
    flow.references.push_back({read,binding,FlowReferenceKind::kRead,f.reads[n]});
  }
  for(unsigned i=0;i+1<size;++i) {
    const auto from=point(i),to=point(i+1);
    flow.edges.push_back({from,to,FlowCompletion::kNormal,flow.events[to.value].incoming,flow.events[from.value].outgoing});
    flow.events[to.value].incoming=flow.events[from.value].outgoing=flow.edges.size()-1;
  }
  if(form==7)flow.scopes.reset();
  const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(flow));
  const auto solve=[&](analysis::SemanticBudget allowance={}) {
    return CheckedFlowNormalKinds(f.tree,flow,live,f.Input(),allowance);
  };
  const auto actual=std::get<FlowNormalKinds>(solve());
  // An explicit pre-boundary saved scalar survives allocation and callbacks.
  // A current-cell dependency sampled after allocation has no old definition.
  const bool known=form==0||form==3||form==4||form==5||form==8;
  for(auto id:f.reads)Check(actual.expressions[id.value]==(known?analysis::ValueKinds::Number():analysis::ValueKinds::Language()));
  const analysis::SemanticBudget exact{actual.allocated_bytes,actual.work};
  Check(std::holds_alternative<FlowNormalKinds>(solve(exact)));
  Check(std::get<TreeStructureError>(solve({exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(solve({exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
  return {actual.work,actual.allocated_bytes};
}
void UnitCapabilityContract() {
  Fixture f(1,false);RecoveryFlow flow{};flow.events.resize(2);flow.bindings.resize(1);
  TreeCheckBudget budget({});auto scopes=MakeFlowScopes(f.tree,budget);
  scopes->events.resize(2,{ScopeId{1},ScopeId{1}});
  const std::array<std::optional<FlowLoopScope>,1> loops{FlowLoopScope{}};
  IndexFlowLoopScopes(*scopes,loops,budget);scopes->loop_events.resize(2,{StmtId{0},StmtId{0}});flow.scopes=scopes;
  const FlowEdge edge{{0},{1},FlowCompletion::kNormal};
  flow.bindings[0].lifetime=EnclosingBindingLifetime{};
  Check(flow.BindingUnit({0})==FlowUnitCapability::kCurrent);
  flow.bindings[0].lifetime=CompletionOnlyLifetime{};
  Check(flow.BindingUnit({0})==FlowUnitCapability::kCompletionOnly);
  flow.bindings[0].lifetime=UnmodeledBindingLifetime{};
  Check(flow.BindingUnit({0})==FlowUnitCapability::kUnmodeled);
  for(bool loop:{false,true}) {
    flow.bindings[0].lifetime=loop?FlowBindingLifetime{LoopBindingLifetime{{0}}}:
        FlowBindingLifetime{ScopeBindingLifetime{{1},{0},{1}}};
    flow.scopes=scopes;Check(flow.BindingUnit({0})==FlowUnitCapability::kCurrent);
    Check(flow.TransferBinding({0},edge)==FlowUnitTransfer::kPreserved);
    flow.scopes.reset();Check(flow.BindingUnit({0})==FlowUnitCapability::kUnmodeled);
    Check(flow.TransferBinding({0},edge)==FlowUnitTransfer::kUnmodeled);
  }
}
// Actual source-adapter execution: a completed conversion initializes a
// native loop unit; callback accessibility is shared with every current/trial
// consumer. Independent JS below supplies the behavior, never engine inputs.
void CurrentUnitSource(bool captured,unsigned count,bool reverse,bool emit,bool update=false) {
  FunctionTree tree{};tree.callable=CallableKind::kFunction;tree.strictness=Strictness::kStrict;
  tree.parameter_scope={0};tree.body_scope={1};tree.body={0};tree.origins={OriginalSite{{0},{0}}};
  tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}},
      {ScopeKind::kBlock,ScopeId{1}},{ScopeKind::kBlock,ScopeId{2}},
      {ScopeKind::kBlock,ScopeId{3}},{ScopeKind::kCatch,ScopeId{1}}};
  tree.names={u"input",u"report",u"current",u"error",u"callback"};
  tree.symbols={{{0},{0},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}},
      {{0},{1},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}},
      {{1},{2},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}},
      {{5},{3},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}},
      {{1},{4},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}}};
  tree.parameters={{{0},{},false},{{1},{},false}};
  const auto expr=[&](TreeExpression node){ExprId id{static_cast<unsigned>(tree.expressions.size())};tree.expressions.push_back({std::move(node),{0}});return id;};
  const auto stmt=[&](TreeStatement node){StmtId id{static_cast<unsigned>(tree.statements.size())};tree.statements.push_back({std::move(node),{0}});return id;};
  const auto block=[&](ScopeId scope,const std::vector<StmtId>& children) {
    const auto first=tree.children.size();tree.children.insert(tree.children.end(),children.begin(),children.end());
    return stmt(BlockStmt{scope,static_cast<unsigned>(first),static_cast<unsigned>(children.size())});
  };
  const auto read=[&](unsigned symbol){return expr(SymbolExpr{{symbol}});};
  const auto integer=[&](std::int32_t value){return expr(TreeLiteral{IntegerLiteral{value}});};
  const auto call=[&](unsigned callee,std::optional<ExprId> argument={}) {
    const auto first=tree.arguments.size();if(argument)tree.arguments.push_back(*argument);
    return expr(CallExpr{read(callee),{static_cast<unsigned>(first),argument?1U:0U},CallReference::kValue});
  };
  const auto root=stmt(BlockStmt{}),loop=stmt(LoopStmt{});
  const auto declaration=stmt(DeclarationStmt{{2},{}});
  const auto callback=stmt(DeclarationStmt{{4},read(1)});
  const auto converted=expr(UnaryExpr{TreeUnary::kPlus,call(0)});
  const auto entry=stmt(ExpressionStmt{expr(AssignmentExpr{SymbolTarget{{2}},converted})});
  const auto condition=expr(BinaryExpr{analysis::BinaryOperator::kGreater,read(2),integer(0)});
  std::vector<StmtId> body{stmt(IfStmt{expr(UnaryExpr{TreeUnary::kNot,condition}),block({4},{stmt(BreakStmt{loop,{}})}),{}})};
  if(captured)tree.closure_captures={{2},{4}};
  body.push_back(stmt(ExpressionStmt{call(1,captured?expr(FunctionExpr{{1},{0,2}}):read(2))}));
  for(unsigned i=0;i<count;++i) {
    const ScopeId scope{static_cast<unsigned>(tree.scopes.size())};
    tree.scopes.push_back({ScopeKind::kBlock,ScopeId{3}});body.push_back(block(scope,{}));
  }
  body.push_back(stmt(ExpressionStmt{expr(UnaryExpr{TreeUnary::kNegate,read(2)})}));
  body.push_back(stmt(ExpressionStmt{call(1,expr(UnaryExpr{TreeUnary::kNegate,read(2)}))}));
  if(update)body.push_back(stmt(ExpressionStmt{expr(UpdateExpr{{2},UpdateDirection::kDecrement,UpdateResult::kPostfix})}));
  else {
    const auto next=expr(BinaryExpr{analysis::BinaryOperator::kSubtract,read(2),integer(1)});
    body.push_back(stmt(ExpressionStmt{expr(AssignmentExpr{SymbolTarget{{2}},next})}));
  }
  body.push_back(stmt(ContinueStmt{loop,{}}));
  std::get<LoopStmt>(tree.statements[loop.value].statement).body=block({3},body);
  const auto handler=block({5},{stmt(ExpressionStmt{call(1,read(3))})});
  const auto attempt=block({2},{entry,loop});
  const auto first=tree.children.size();tree.children.insert(tree.children.end(),{declaration,callback,stmt(TryStmt{attempt,CatchClause{SymbolId{3},handler},{}}),stmt(ReturnStmt{integer(0)})});
  std::get<BlockStmt>(tree.statements[root.value].statement)={{1},static_cast<unsigned>(first),4};
  if(reverse) {
    const auto map=[&](StmtId id){return StmtId{static_cast<unsigned>(tree.statements.size()-1-id.value)};};
    for(auto& child:tree.children)child=map(child);
    for(auto& item:tree.statements)std::visit([&](auto& node){using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,LoopStmt>)node.body=map(node.body);
      else if constexpr(std::is_same_v<T,IfStmt>){node.consequent=map(node.consequent);if(node.alternate)node.alternate=map(*node.alternate);}
      else if constexpr(std::is_same_v<T,BreakStmt>||std::is_same_v<T,ContinueStmt>)node.target=map(node.target);
      else if constexpr(std::is_same_v<T,TryStmt>){node.body=map(node.body);if(node.handler)node.handler->body=map(node.handler->body);}
    },item.statement);
    tree.body=map(tree.body);std::reverse(tree.statements.begin(),tree.statements.end());
  }
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  OriginalFlow original{};original.identity=tree.identity;original.method=tree.method;original.value_revision=tree.value_revision;original.cfg_revision=tree.cfg_revision;
  original.statements=tree.statements.size();original.bindings=tree.symbols.size();original.input_ranges.resize(tree.expressions.size());
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto plan=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{},&original));
  if(count==0) {
    Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes,plan.work},&original)));
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes-1,plan.work},&original))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes,plan.work-1},&original))==TreeStructureError::kWorkLimit);
  }
  std::ostringstream output;
  if(captured) {
    FunctionTree child{};child.owner={1};child.callable=CallableKind::kFunction;child.strictness=Strictness::kStrict;
    child.names={u"current",u"report"};child.parameter_scope={0};child.body_scope={1};child.capture_scope=ScopeId{2};child.body={0};
    child.scopes={{ScopeKind::kParameters,ScopeId{2}},{ScopeKind::kFunctionBody,ScopeId{0}},{ScopeKind::kCaptures,{}}};
    child.symbols={{{2},{0},BindingKind::kCapture,DeclarationKind::kLet,NameEvidence::kMetadata,{}},{{2},{1},BindingKind::kCapture,DeclarationKind::kLet,NameEvidence::kMetadata,{}}};
    child.captures={{{0},{tree.owner,{2}}},{{1},{tree.owner,{4}}}};child.origins={OriginalSite{{0},{0}}};
    child.texts={u"set"};
    child.expressions={{SymbolExpr{{1}},{0}},{TreeLiteral{TextId{0}},{0}},
      {CallExpr{{0},{0,1},CallReference::kValue},{0}},{AssignmentExpr{SymbolTarget{{0}},ExprId{2}},{0}}};child.arguments={{1}};
    child.statements={{BlockStmt{{1},0,1},{0}},{ExpressionStmt{{3}},{0}}};child.children={{1}};
    ConstructedFamily family;family.root={1};family.members={{std::move(child),{},{}},{tree,{}, {}}};
    Check(std::holds_alternative<PrintedFamilyDraft>(PrintFamilyDraft(family,output)));
  } else Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output,{}, {},nullptr,&original)));
  if(emit)std::cout<<nlohmann::json{{"source",output.str()},{"captured",captured},{"count",count},{"reverse",reverse},
      {"work",plan.work},{"bytes",plan.allocated_bytes},{"update",update}}<<'\n';
}
void UpdateSource(bool reverse,bool numeric,bool increment,bool prefix,bool emit,bool caught=false) {
  Fixture f(1,reverse);auto& tree=f.tree;
  const auto expr=[&](TreeExpression node,FlowValueEquation equation=UnknownFlowValue{}) {
    const ExprId id{static_cast<unsigned>(tree.expressions.size())};
    tree.expressions.push_back({std::move(node),{0}});f.equations.push_back(equation);f.order.push_back(id);return id;
  };
  const auto stmt=[&](TreeStatement node) {
    const StmtId id{static_cast<unsigned>(tree.statements.size())};tree.statements.push_back({std::move(node),{0}});return id;
  };
  if(!numeric) {
    const auto call=std::get<UnaryExpr>(tree.expressions[f.values[0].value].expression).operand;
    const auto ignored=expr(TreeLiteral{UndefinedLiteral{}},FlowPrimitiveReference{{0}});
    tree.expressions[f.values[0].value].expression=SequenceExpr{ignored,call};f.equations[f.values[0].value]=FlowExpressionValue{call};
  }
  const SymbolId report{2};const NameId name{static_cast<unsigned>(tree.names.size())};tree.names.push_back(u"report");
  tree.symbols.push_back({{0},name,BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}});
  tree.parameters.push_back({report,{},false});
  const auto update=expr(UpdateExpr{{1},increment?UpdateDirection::kIncrement:UpdateDirection::kDecrement,
      prefix?UpdateResult::kPrefix:UpdateResult::kPostfix});
  const auto after=expr(SymbolExpr{{1}},FlowBindingValue{{1}});tree.arguments={update,after};
  const auto call=expr(CallExpr{expr(SymbolExpr{report},FlowBindingValue{report}),{0,2},CallReference::kValue});
  tree.children.insert(tree.children.begin()+2,stmt(ExpressionStmt{call}));
  tree.children.push_back(stmt(ReturnStmt{expr(SymbolExpr{{1}},FlowBindingValue{{1}})}));
  std::get<BlockStmt>(tree.statements[0].statement).child_count=tree.children.size();
  if(caught) {
    const auto roots=tree.children;tree.children.clear();
    tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});tree.scopes.push_back({ScopeKind::kCatch,ScopeId{1}});
    const NameId error_name{static_cast<unsigned>(tree.names.size())};tree.names.push_back(u"error");
    tree.symbols.push_back({{3},error_name,BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
    const auto block=[&](ScopeId scope,std::vector<StmtId> children) {
      const auto first=tree.children.size();tree.children.insert(tree.children.end(),children.begin(),children.end());
      return stmt(BlockStmt{scope,static_cast<unsigned>(first),static_cast<unsigned>(children.size())});
    };
    const auto body=block({2},{roots[2],roots[3]});
    tree.arguments.push_back(expr(SymbolExpr{{1}},FlowBindingValue{{1}}));
    tree.arguments.push_back(expr(SymbolExpr{{3}},FlowBindingValue{{3}}));
    const auto handler=block({3},{stmt(ExpressionStmt{expr(CallExpr{expr(SymbolExpr{report},FlowBindingValue{report}),{2,2},CallReference::kValue})})});
    const auto attempt=stmt(TryStmt{body,CatchClause{SymbolId{3},handler},{}});
    const auto first=tree.children.size();tree.children.insert(tree.children.end(),{roots[0],roots[1],attempt,roots[4]});
    std::get<BlockStmt>(tree.statements[0].statement)={{1},static_cast<unsigned>(first),4};
  }
  const auto x=tree.expressions.size(),s=tree.statements.size(),b=tree.symbols.size();
  f.plan.aliases.resize(b);f.plan.storage.resize(b);f.plan.read_bindings.resize(b);f.plan.literals.resize(b);f.private_bindings.resize(b,0);
  f.plan.read_sources.resize(x);f.plan.read_literals.resize(x);f.plan.replacements.resize(x);f.plan.transports.resize(x);
  f.plan.omitted.resize(s);f.plan.statements.resize(s);f.original.statements=s;f.original.bindings=b;
  f.original.input_ranges.resize(x);
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto model=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,f.Projection(),f.Input()));
  for(const auto expression:{update,after,f.reads[0]})
    Check(model.expressions.kinds[expression.value]==(numeric?analysis::ValueKinds::Number():analysis::ValueKinds::Language()));
  bool captured=false;
  for(const auto& definition:model.flow.definitions)if(definition.kind==FlowDefinitionKind::kUpdate) {
    Check(definition.input&&definition.input->binding==SymbolId{1});
    const auto& read=model.flow.events[definition.input->point.value];
    Check(read.kind==FlowEventKind::kRead);captured=true;
    // Initialization discharges this GetValue, not the later ToNumeric.
    Check(read.effect==ExpressionEvaluation::kInert);
    for(auto edge=read.outgoing;edge;edge=model.flow.edges[*edge].outgoing)
      Check(model.flow.edges[*edge].completion!=FlowCompletion::kThrow);
  }
  Check(captured);
  for(const auto& event:model.flow.events)if(event.kind==FlowEventKind::kEvaluation&&event.expression==update) {
    Check(event.input&&event.input->value<model.flow.captured_inputs.size());
    const auto& input=model.flow.captured_inputs[event.input->value];
    Check(input.kinds==(numeric?analysis::ValueKinds::Number():analysis::ValueKinds::Language()));
    for(const auto& definition:model.flow.definitions)if(definition.kind==FlowDefinitionKind::kUpdate)Check(definition.input==input.value);
    Check(event.effects.has_value());
    Check(HasLanguageEffect(event.effects->normal,analysis::LanguageEffect::kUserCode)==!numeric);
    Check((event.effects->exceptional==analysis::ExceptionalCompletion::kImpossible)==numeric);
    if(numeric)for(auto edge=event.outgoing;edge;edge=model.flow.edges[*edge].outgoing)
      Check(model.flow.edges[*edge].completion!=FlowCompletion::kThrow);
  }
  // Whole update retention includes the success-only write independently of
  // whether converting the captured input can invoke callbacks or throw.
  Check(model.expressions.evaluations[update.value]==ExpressionEvaluation::kRequired);
  auto source_input=f.Input();source_input.original=nullptr;
  auto source_prepared=PrepareRecoveryProjection(tree,f.Projection(),source_input);
  Check(std::holds_alternative<RecoveryProjection>(source_prepared));
  const auto source_only=std::get<RecoveryProjection>(std::move(source_prepared));
  Check(source_only.expressions.kinds==model.expressions.kinds);
  Check(source_only.flow.captured_inputs.size()==model.flow.captured_inputs.size());
  for(std::size_t i=0;i<model.flow.captured_inputs.size();++i)
    Check(source_only.flow.captured_inputs[i].kinds==model.flow.captured_inputs[i].kinds);
  const analysis::SemanticBudget exact{model.allocated_bytes,model.work};
  Check(std::holds_alternative<RecoveryProjection>(PrepareRecoveryProjection(tree,f.Projection(),f.Input(),exact)));
  Check(std::get<TreeStructureError>(PrepareRecoveryProjection(tree,f.Projection(),f.Input(),{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(PrepareRecoveryProjection(tree,f.Projection(),f.Input(),{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
  if(numeric) {
    auto replacements=f.plan.replacements;
    replacements[f.values[0].value]=std::get<UnaryExpr>(tree.expressions[f.values[0].value].expression).operand;
    auto candidate=BindingPlanView{f.plan};candidate.replacements=replacements;
    const auto trial=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,f.Projection(),{candidate,f.equations,f.order,&f.original}));
    for(const auto expression:{update,after,f.reads[0]})Check(trial.expressions.kinds[expression.value]==analysis::ValueKinds::Language());
    Check(trial.expressions.evaluations[f.results[0].value]==ExpressionEvaluation::kRequired);
    Check(model.expressions.evaluations[f.results[0].value]==ExpressionEvaluation::kInert);
    for(const auto& event:trial.flow.events)if(event.kind==FlowEventKind::kEvaluation&&event.expression==update)
      Check(event.effects&&HasLanguageEffect(event.effects->normal,analysis::LanguageEffect::kUserCode));
  }
  std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output,{}, {},nullptr,&f.original)));
  if(emit)std::cout<<nlohmann::json{{"source",output.str()},{"numeric",numeric},{"increment",increment},{"prefix",prefix},{"reverse",reverse},{"caught",caught}}<<'\n';
}
// Closure is a property of the published product, independent of source layout.
// A value can dominate a syntactically retained read whose completion is
// impossible. Skipping transport must not publish that value for the dead read.
void SparseCompletionGuard(bool reverse,unsigned hops) {
  Fixture f(1,false);RecoveryFlow flow{};flow.original=&f.original;
  flow.bindings.resize(2);flow.bindings[0]={BindingKind::kParameter};
  flow.bindings[1]={BindingKind::kCompilerTemporary,FlowBindingExposure::kPrivate};
  const unsigned count=4+hops;
  const auto point=[&](unsigned id){return FlowPoint{reverse?count-1-id:id};};
  flow.events.resize(count);flow.entry=point(0);flow.terminal=point(count-1);
  const auto number=*std::get<DeclarationStmt>(f.tree.statements[f.declarations[0].value].statement).initializer;
  flow.definitions.push_back({point(0),{1},FlowDefinitionKind::kInitializer,{},number});
  flow.events[point(0).value].kind=FlowEventKind::kDefinition;flow.events[point(0).value].definition=FlowDefinitionId{0};
  flow.events[point(1).value].kind=FlowEventKind::kEvaluation;flow.events[point(1).value].expression=number;
  const auto read=point(count-2);flow.events[read.value].kind=FlowEventKind::kRead;
  flow.events[read.value].expression=f.reads[0];flow.events[read.value].read=SymbolId{1};
  flow.references.push_back({read,{1},FlowReferenceKind::kRead,f.reads[0]});
  const auto edge=[&](unsigned from,unsigned to,FlowCompletion completion) {
    const auto source=point(from),target=point(to);const auto id=flow.edges.size();
    flow.edges.push_back({source,target,completion,flow.events[target.value].incoming,flow.events[source.value].outgoing});
    flow.events[target.value].incoming=id;flow.events[source.value].outgoing=id;
  };
  edge(0,1,FlowCompletion::kNormal);edge(1,count-1,FlowCompletion::kNormal);edge(1,2,FlowCompletion::kThrow);
  for(unsigned i=2;i+1<count;++i)edge(i,i+1,FlowCompletion::kNormal);
  std::vector<ExpressionEvaluation> effects(f.tree.expressions.size(),ExpressionEvaluation::kInert);
  std::vector<std::uint8_t> stores(f.tree.expressions.size());
  const FlowEffectInput completion{effects,stores,effects,{}};
  const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(flow));
  const auto result=std::get<FlowNormalKinds>(CheckedFlowNormalKinds(f.tree,flow,live,f.Input(),{},&completion));
  Check(!result.executable[2]);Check(result.expressions[f.reads[0].value]==analysis::ValueKinds::Language());
}
void SparseStateBudgets() {
  Fixture fixture(4,true);
  const auto projection=std::get<RecoveryProjection>(PrepareRecoveryProjection(fixture.tree,fixture.Projection(),fixture.Input()));
  const auto& flow=projection.flow;
  const auto state=std::get<FlowValueStates>(BuildFlowValueStates(flow));
  Check(state.retained_bytes<state.allocated_bytes);
  const analysis::SemanticBudget exact{state.allocated_bytes,state.work};
  Check(std::holds_alternative<FlowValueStates>(BuildFlowValueStates(flow,exact)));
  Check(std::get<TreeStructureError>(BuildFlowValueStates(flow,{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(BuildFlowValueStates(flow,{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
  const auto other=flow;
  Check(std::get<TreeStructureError>(AnalyzeFlowNormalKinds(fixture.tree,other,state,fixture.Input()))==TreeStructureError::kOwnership);
  // Operand metadata alone is not a conditional result/definition proof.
  // It must not create a validity propagation component with no consumers.
  const auto metadata=std::get<FlowNormalKinds>(AnalyzeFlowNormalKinds(fixture.tree,flow,state,fixture.Input()));
  auto source_flow=flow;source_flow.original=nullptr;
  auto source_input=fixture.Input();source_input.original=nullptr;
  const auto source_states=std::get<FlowValueStates>(BuildFlowValueStates(source_flow));
  const auto source=std::get<FlowNormalKinds>(AnalyzeFlowNormalKinds(fixture.tree,source_flow,source_states,source_input));
  Check(metadata.expressions==source.expressions);
  Check(metadata.work==source.work&&metadata.allocated_bytes==source.allocated_bytes);
}
// A deliberately conservative result equation remains Language while a loop
// widens the captured input. Effects must wake from the input itself, even for
// a partial graph client that has not published numeric result correspondence.
void CapturedEffectClosure(bool reverse,bool unknown) {
  Fixture f(1,false);auto& tree=f.tree;
  const ExprId update{static_cast<unsigned>(tree.expressions.size())};
  tree.expressions.push_back({UpdateExpr{{1},UpdateDirection::kIncrement,UpdateResult::kPostfix},{0}});
  f.equations.push_back(UnknownFlowValue{});f.order.push_back(update);
  const auto count=tree.expressions.size();
  f.plan.read_sources.resize(count);f.plan.read_literals.resize(count);f.plan.replacements.resize(count);f.plan.transports.resize(count);
  f.original.input_ranges.resize(count);
  RecoveryFlow flow{};flow.original=&f.original;flow.bindings.resize(2);
  flow.bindings[0]={BindingKind::kParameter};flow.bindings[1]={BindingKind::kCompilerTemporary,FlowBindingExposure::kPrivate};
  constexpr unsigned points=6;
  const auto point=[&](unsigned id){return FlowPoint{reverse?points-1-id:id};};
  flow.events.resize(points);flow.entry=point(0);flow.terminal=point(5);
  const auto number=*std::get<DeclarationStmt>(tree.statements[f.declarations[0].value].statement).initializer;
  const auto replacement=unknown?std::get<UnaryExpr>(tree.expressions[f.values[0].value].expression).operand:number;
  flow.definitions={{point(0),{1},FlowDefinitionKind::kInitializer,{},number},
      {point(3),{1},FlowDefinitionKind::kAssignment,{},replacement}};
  for(unsigned i:{0U,3U}) {
    flow.events[point(i).value].kind=FlowEventKind::kDefinition;
    flow.events[point(i).value].definition=FlowDefinitionId{i==0?0U:1U};
  }
  flow.events[point(1).value].kind=FlowEventKind::kRead;flow.events[point(1).value].read=SymbolId{1};
  flow.events[point(2).value].kind=FlowEventKind::kEvaluation;flow.events[point(2).value].expression=update;
  flow.events[point(2).value].input=FlowInputId{0};
  flow.captured_inputs.push_back({point(2),{point(1),{1}}});
  const auto edge=[&](unsigned from,unsigned to,FlowCompletion completion=FlowCompletion::kNormal) {
    const auto source=point(from),target=point(to);const auto id=flow.edges.size();
    flow.edges.push_back({source,target,completion,flow.events[target.value].incoming,flow.events[source.value].outgoing});
    flow.events[target.value].incoming=id;flow.events[source.value].outgoing=id;return id;
  };
  edge(0,1);edge(1,2);edge(2,3);edge(3,4);edge(4,1);const auto thrown=edge(2,5,FlowCompletion::kThrow);
  std::vector<ExpressionEvaluation> evaluations(count,ExpressionEvaluation::kRequired);
  std::vector<std::uint8_t> stores(count);const FlowEffectInput completion{evaluations,stores,evaluations,{}};
  const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(flow));
  const auto solved=std::get<FlowNormalKinds>(CheckedFlowNormalKinds(tree,flow,live,f.Input(),{},&completion));
  Check(solved.expressions[update.value]==analysis::ValueKinds::Language());
  Check(solved.captured_inputs[0]==(unknown?analysis::ValueKinds::Language():analysis::ValueKinds::Number()));
  Check(static_cast<bool>(solved.executable[thrown])==unknown);
  auto stale=completion;stale.input_kinds=solved.captured_inputs;
  Check(std::get<TreeStructureError>(CheckedFlowNormalKinds(tree,flow,live,f.Input(),{},&stale))==TreeStructureError::kOwnership);
}
std::uint64_t CapturedInputCosts(unsigned count,bool reverse) {
  Fixture f(count,reverse);
  for(unsigned i=0;i<count;++i) {
    f.tree.expressions[f.results[i].value].expression=UpdateExpr{{i+1},UpdateDirection::kIncrement,UpdateResult::kPostfix};
    f.equations[f.results[i].value]=UnknownFlowValue{};
  }
  auto prepared=PrepareRecoveryProjection(f.tree,f.Projection(),f.Input());
  if(const auto* error=std::get_if<TreeStructureError>(&prepared))std::cerr<<"captured cost error="<<static_cast<unsigned>(*error)<<" count="<<count<<'\n';
  Check(std::holds_alternative<RecoveryProjection>(prepared));
  const auto model=std::get<RecoveryProjection>(std::move(prepared));
  Check(model.flow.captured_inputs.size()==count);
  for(const auto& input:model.flow.captured_inputs)Check(input.kinds==analysis::ValueKinds::Number());
  if(count<=201) {
    const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(model.flow));
    const auto oracle=std::get<FlowNormalKinds>(CheckedFlowNormalKinds(f.tree,model.flow,live,f.Input()));
    Check(oracle.expressions==model.expressions.kinds);
    for(const auto& input:oracle.captured_inputs)Check(input==analysis::ValueKinds::Number());
    const analysis::SemanticBudget exact{model.allocated_bytes,model.work};
    Check(std::holds_alternative<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input(),exact)));
    Check(std::get<TreeStructureError>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input(),{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
    Check(std::get<TreeStructureError>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input(),{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
  }
  std::cerr<<"captured inputs count="<<count<<" reverse="<<reverse<<" events="<<model.flow.events.size()<<" edges="<<model.flow.edges.size()
      <<" work="<<model.work<<" bytes="<<model.allocated_bytes<<'\n';
  return model.work;
}
// Chained ordinary try/assignment fragments expose stale domains after final
// effect pruning; a fixed extra number of whole-graph rounds cannot satisfy it.
std::pair<std::uint64_t,std::size_t> ProjectionClosure(unsigned depth,bool known=true,bool reverse=false,bool emit=false,bool late_initialization=false,bool original=true) {
  Check(depth>0);
  Fixture f(0,false);auto& t=f.tree;
  const auto expr=[&](TreeExpression v,FlowValueEquation q=UnknownFlowValue{}){ExprId x{static_cast<unsigned>(t.expressions.size())};t.expressions.push_back({v,{0}});f.equations.push_back(q);f.order.push_back(x);return x;};
  const auto stmt=[&](TreeStatement v){StmtId x{static_cast<unsigned>(t.statements.size())};t.statements.push_back({v,{0}});return x;};
  const auto block=[&](ScopeId scope,std::vector<StmtId> children){auto first=t.children.size();t.children.insert(t.children.end(),children.begin(),children.end());return stmt(BlockStmt{scope,static_cast<unsigned>(first),static_cast<unsigned>(children.size())});};
  t.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});t.scopes.push_back({ScopeKind::kCatch,ScopeId{1}});
  for(const auto name:{u"number",u"saved",u"error"})t.names.push_back(name);
  t.symbols.push_back({{1},{1},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
  t.symbols.push_back({{1},{2},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
  t.symbols.push_back({{3},{3},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
  std::optional<SymbolId> late;std::optional<ExprId> late_read;
  if(late_initialization) {
    late=SymbolId{static_cast<unsigned>(t.symbols.size())};
    const NameId name{static_cast<unsigned>(t.names.size())};t.names.push_back(u"later");
    t.symbols.push_back({{1},name,BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
    late_read=expr(SymbolExpr{*late},FlowBindingValue{*late});
  }
  const auto input=expr(SymbolExpr{{0}},FlowBindingValue{{0}});
  const auto call=expr(CallExpr{input,{0,0},CallReference::kValue});
  const auto number=known?expr(UnaryExpr{TreeUnary::kPlus,call}):call;
  const auto first=stmt(DeclarationStmt{{1},number}),second=stmt(DeclarationStmt{{2},{}});
  const auto read=expr(SymbolExpr{{1}},FlowBindingValue{{1}});
  const auto plus=expr(UnaryExpr{TreeUnary::kPlus,read});
  const auto literal=expr(TreeLiteral{IntegerLiteral{7}},FlowPrimitiveReference{{1}});
  const auto write=expr(AssignmentExpr{SymbolTarget{{2}},literal},FlowExpressionValue{literal});
  const auto body=block({2},{stmt(ExpressionStmt{plus}),stmt(ExpressionStmt{write})});
  const auto handler=block({3},late_read?std::vector<StmtId>{stmt(ExpressionStmt{*late_read})}:std::vector<StmtId>{});
  const auto attempt=stmt(TryStmt{body,CatchClause{SymbolId{3},handler},{}});
  std::vector<StmtId> sequence{first,second,attempt};
  SymbolId prior{2};
  for(unsigned i=1;i<depth;++i) {
    const SymbolId binding{static_cast<unsigned>(t.symbols.size())};
    const NameId name{static_cast<unsigned>(t.names.size())};const auto suffix=std::to_string(i);t.names.push_back(u"next"+std::u16string(suffix.begin(),suffix.end()));
    t.symbols.push_back({{1},name,BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
    const ScopeId scope{static_cast<unsigned>(t.scopes.size())};t.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
    const ScopeId caught{static_cast<unsigned>(t.scopes.size())};t.scopes.push_back({ScopeKind::kCatch,ScopeId{1}});
    const auto declaration=stmt(DeclarationStmt{binding,{}});
    const auto read_prior=expr(SymbolExpr{prior},FlowBindingValue{prior});
    const auto convert=expr(UnaryExpr{TreeUnary::kPlus,read_prior});
    const auto value=expr(TreeLiteral{IntegerLiteral{7}},FlowPrimitiveReference{{1}});
    const auto assign=expr(AssignmentExpr{SymbolTarget{binding},value},FlowExpressionValue{value});
    const auto body_next=block(scope,{stmt(ExpressionStmt{convert}),stmt(ExpressionStmt{assign})});
    const SymbolId catch_symbol{static_cast<unsigned>(t.symbols.size())};
    const NameId catch_name{static_cast<unsigned>(t.names.size())};t.names.push_back(u"error"+std::u16string(suffix.begin(),suffix.end()));
    t.symbols.push_back({caught,catch_name,BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
    const auto catch_next=block(caught,{});
    sequence.push_back(declaration);sequence.push_back(stmt(TryStmt{body_next,CatchClause{catch_symbol,catch_next},{}}));
    prior=binding;
  }
  auto result=expr(SymbolExpr{prior},FlowBindingValue{prior});
  const auto returned=stmt(ReturnStmt{expr(UnaryExpr{TreeUnary::kNegate,result})});
  if(late)sequence.push_back(stmt(DeclarationStmt{*late,expr(TreeLiteral{IntegerLiteral{7}},FlowPrimitiveReference{{1}})}));
  const auto begin=t.children.size();sequence.push_back(returned);t.children.insert(t.children.end(),sequence.begin(),sequence.end());
  std::get<BlockStmt>(t.statements[0].statement)={{1},static_cast<unsigned>(begin),static_cast<unsigned>(sequence.size())};
  const auto b=t.symbols.size(),e=t.expressions.size(),s=t.statements.size();
  f.plan.aliases.resize(b);f.plan.storage.resize(b);f.plan.read_bindings.resize(b);f.plan.literals.resize(b);f.private_bindings.resize(b,1);f.private_bindings[0]=0;
  f.plan.read_sources.resize(e);f.plan.read_literals.resize(e);f.plan.replacements.resize(e);f.plan.transports.resize(e);f.plan.omitted.resize(s);f.plan.statements.resize(s);
  f.original.statements=s;f.original.bindings=b;f.original.input_ranges.resize(e);
  if(reverse) {
    const auto map=[&](ExprId id){return ExprId{static_cast<unsigned>(e-1-id.value)};};
    for(auto& item:t.expressions)std::visit([&](auto& node){using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,CallExpr>)node.callee=map(node.callee);
      else if constexpr(std::is_same_v<T,AssignmentExpr>)node.value=map(node.value);
      else if constexpr(std::is_same_v<T,UnaryExpr>)node.operand=map(node.operand);
    },item.expression);
    for(auto& item:t.statements)std::visit([&](auto& node){using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,DeclarationStmt>){if(node.initializer)node.initializer=map(*node.initializer);}
      else if constexpr(std::is_same_v<T,ExpressionStmt>)node.expression=map(node.expression);
      else if constexpr(std::is_same_v<T,ReturnStmt>){if(node.value)node.value=map(*node.value);}
    },item.statement);
    for(auto& equation:f.equations)if(auto* value=std::get_if<FlowExpressionValue>(&equation))value->expression=map(value->expression);
    for(auto& id:f.order)id=map(id);
    std::reverse(t.expressions.begin(),t.expressions.end());std::reverse(f.equations.begin(),f.equations.end());
    result=map(result);if(late_read)late_read=map(*late_read);
  }
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(t)));
  auto projection=f.Projection();auto projected_input=f.Input();
  if(!original){projection.original=nullptr;projected_input.original=nullptr;}
  const auto model=std::get<RecoveryProjection>(PrepareRecoveryProjection(t,projection,projected_input));
  const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(model.flow));
  const auto final=std::get<FlowNormalKinds>(CheckedFlowNormalKinds(t,model.flow,live,projected_input));
  Check(model.expressions.kinds==final.expressions);
  Check(model.expressions.kinds[result.value].IsNumber()==known);
  const auto initialized=std::get<FlowInitializations>(AnalyzeFlowInitializations(model.flow));
  bool seen_late=false;
  for(const auto& reference:model.flow.references) {
    const bool completed=initialized.Completed(reference.binding,reference.point);
    if(const auto* site=std::get_if<ExprId>(&reference.site)) {
      Check(model.initialized_reads[site->value]==completed);
      seen_late|=late_read&&*site==*late_read;
    }
    if(reference.kind==FlowReferenceKind::kRead)
      Check((model.flow.events[reference.point.value].read_completion==FlowReadCompletion::kInitialized)==completed);
  }
  Check(!late_read||seen_late);
  if(depth==1) {
    const auto equations=std::get<FlowInitializationProgram>(BuildFlowInitializationProgram(model.flow));
    const auto other_projection=model.flow;
    const FlowEffectInput effects{model.expressions.evaluations,model.expressions.normal_stores,model.expressions.evaluations,{}};
    Check(std::get<TreeStructureError>(CheckedFlowNormalKinds(t,other_projection,live,projected_input,{},&effects,&equations))==TreeStructureError::kOwnership);
  }
  if(emit) {
    std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(t,output,{}, {},nullptr,original?&f.original:nullptr)));
    std::cout<<nlohmann::json{{"source",output.str()},{"depth",depth},{"known",known},{"reverse",reverse},{"original",original}}<<'\n';
  }
  return {model.work,model.allocated_bytes};
}
// The only feasible catch payload is literal 7: parameter conversion is
// outside try, the Number conversion cannot throw, and its arm returns. A
// retained original result proof is therefore finite even when an inert
// statement in that returning arm is omitted. Impossible failures must not
// permanently invalidate the proof published for the reachable handler.
enum class RawClosureEdit {kNone,kUnknownRead,kChangedThrowPath};
std::pair<std::uint64_t,std::size_t> RawProjectionClosure(bool changed,RawClosureEdit edit=RawClosureEdit::kNone,bool reverse=false,unsigned copies=1) {
  Fixture f(0,false);auto& t=f.tree;
  const auto expr=[&](TreeExpression v,FlowValueEquation q=UnknownFlowValue{}){ExprId x{static_cast<unsigned>(t.expressions.size())};t.expressions.push_back({v,{0}});f.equations.push_back(q);f.order.push_back(x);return x;};
  const auto stmt=[&](TreeStatement v){StmtId x{static_cast<unsigned>(t.statements.size())};t.statements.push_back({v,{0}});return x;};
  const auto block=[&](ScopeId scope,std::vector<StmtId> children){auto first=t.children.size();t.children.insert(t.children.end(),children.begin(),children.end());return stmt(BlockStmt{scope,static_cast<unsigned>(first),static_cast<unsigned>(children.size())});};
  t.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});t.scopes.push_back({ScopeKind::kBlock,ScopeId{2}});t.scopes.push_back({ScopeKind::kCatch,ScopeId{1}});
  t.names.push_back(u"number");t.names.push_back(u"error");
  t.symbols.push_back({{1},{1},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
  t.symbols.push_back({{4},{2},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
  const auto value=expr(UnaryExpr{TreeUnary::kPlus,expr(SymbolExpr{{0}},FlowBindingValue{{0}})});
  const auto declare=stmt(DeclarationStmt{{1},value});
  const auto unused=stmt(ExpressionStmt{expr(TreeLiteral{IntegerLiteral{7}},FlowPrimitiveReference{{1}})});
  const auto numeric_read=expr(SymbolExpr{{1}},FlowBindingValue{{1}});
  const auto conversion=stmt(ExpressionStmt{expr(UnaryExpr{TreeUnary::kPlus,numeric_read})});
  const auto returned=stmt(ReturnStmt{expr(TreeLiteral{IntegerLiteral{7}},FlowPrimitiveReference{{1}})});
  std::vector<StmtId> removable{unused};
  for(unsigned i=1;i<copies;++i)removable.push_back(stmt(ExpressionStmt{expr(TreeLiteral{IntegerLiteral{7}},FlowPrimitiveReference{{1}})}));
  auto arm_statements=removable;arm_statements.push_back(conversion);arm_statements.push_back(returned);
  const auto arm=block({3},arm_statements);
  const auto branch=stmt(IfStmt{expr(SymbolExpr{{0}},FlowBindingValue{{0}}),arm,{}});
  const auto thrown=stmt(ThrowStmt{expr(TreeLiteral{IntegerLiteral{7}},FlowPrimitiveReference{{1}})});
  const auto throw_marker=stmt(ExpressionStmt{expr(TreeLiteral{IntegerLiteral{7}},FlowPrimitiveReference{{1}})});
  const auto body=block({2},{branch,throw_marker,thrown});
  auto caught_read=expr(SymbolExpr{{2}},FlowBindingValue{{2}});
  auto result=expr(UnaryExpr{TreeUnary::kNegate,caught_read});
  const auto handler=block({4},{stmt(ReturnStmt{result})});
  const auto attempt=stmt(TryStmt{body,CatchClause{SymbolId{2},handler},{}});
  const auto first=t.children.size();t.children.insert(t.children.end(),{declare,attempt});std::get<BlockStmt>(t.statements[0].statement)={{1},static_cast<unsigned>(first),2};
  const auto b=t.symbols.size(),e=t.expressions.size(),s=t.statements.size();
  f.plan.aliases.resize(b);f.plan.storage.resize(b);f.plan.read_bindings.resize(b);f.plan.literals.resize(b);f.private_bindings.resize(b,1);f.private_bindings[0]=0;
  f.plan.read_sources.resize(e);f.plan.read_literals.resize(e);f.plan.replacements.resize(e);f.plan.transports.resize(e);f.plan.omitted.resize(s);f.plan.statements.resize(s);for(auto site:removable)f.plan.omitted[site.value]=changed;
  if(edit==RawClosureEdit::kUnknownRead)f.plan.read_sources[numeric_read.value]=SymbolId{0};
  if(edit==RawClosureEdit::kChangedThrowPath)f.plan.omitted[throw_marker.value]=1;
  f.original.statements=s;f.original.bindings=b;f.original.input_ranges.resize(e);
  f.original.values.push_back({analysis::EntryDefinition{analysis::ParameterEntry{0}},analysis::ValueKinds::Language()});
  // This model fixture supplies explicit phase correspondence. A raw Catch
  // domain without its pending-exception entry cannot refine the source join.
  f.original.phases={{{0},{0},analysis::ExceptionPhase::kBefore,analysis::OpId{0}},
      {{1},{0},analysis::ExceptionPhase::kCommit,analysis::OpId{0}},
      {{2},{1},analysis::ExceptionPhase::kCatch,{}},
      {{3},{1},analysis::ExceptionPhase::kBefore,analysis::OpId{1}},
      {{4},{1},analysis::ExceptionPhase::kCommit,analysis::OpId{1}}};
  f.original.values.push_back({analysis::OpResult{{0},analysis::WorkBlockId{1}},analysis::ValueKinds::FiniteNumber()});
  f.original.operations.push_back({{0},{0},0,analysis::ValueId{1},analysis::LanguageEffect::kNone,analysis::LanguageEffect::kNone,analysis::ExceptionalCompletion::kImpossible,analysis::ValueLife::kLive,{0},result,{},result,result});
  f.original.values.push_back({analysis::CatchDefinition{{2}},analysis::ValueKinds::FiniteNumber()});
  f.original.catches.resize(b);f.original.catches[2]=OriginalCatchBinding{{2},{2},handler};
  OriginalOperation raised{};raised.block={3};raised.life=analysis::ValueLife::kLive;
  raised.exceptional=analysis::ExceptionalCompletion::kMayThrow;raised.statement=thrown;
  raised.expression=std::get<ThrowStmt>(t.statements[thrown.value].statement).value;
  raised.abrupt=OriginalAbruptKind::kThrow;f.original.operations.push_back(raised);
  f.original.exceptional_exits.push_back({analysis::OpId{1},{0},analysis::LanguageEffect::kNone,
      analysis::ValueHandler{{{0},{0},{0}},{2}}});
  if(reverse) {
    const auto map=[&](ExprId id){return ExprId{static_cast<unsigned>(e-1-id.value)};};
    for(auto& item:t.expressions)if(auto* unary=std::get_if<UnaryExpr>(&item.expression))unary->operand=map(unary->operand);
    for(auto& item:t.statements)std::visit([&](auto& node){using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,DeclarationStmt>){if(node.initializer)node.initializer=map(*node.initializer);}
      else if constexpr(std::is_same_v<T,ExpressionStmt>)node.expression=map(node.expression);
      else if constexpr(std::is_same_v<T,ReturnStmt>){if(node.value)node.value=map(*node.value);}
      else if constexpr(std::is_same_v<T,ThrowStmt>)node.value=map(node.value);
      else if constexpr(std::is_same_v<T,IfStmt>)node.condition=map(node.condition);
    },item.statement);
    for(auto& id:f.order)id=map(id);
    std::reverse(t.expressions.begin(),t.expressions.end());std::reverse(f.equations.begin(),f.equations.end());
    std::reverse(f.plan.read_sources.begin(),f.plan.read_sources.end());result=map(result);caught_read=map(caught_read);f.original.operations[0].expression=result;f.original.operations[0].result_expression=result;f.original.operations[0].intrinsic_expression=result;
    f.original.operations[1].expression=map(*f.original.operations[1].expression);
  }
  std::vector<std::uint8_t> stable{1,0,0};auto projection=f.Projection();projection.stable_entries=stable;
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(t)));
  const auto model=std::get<RecoveryProjection>(PrepareRecoveryProjection(t,projection,f.Input()));
  const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(model.flow));
  const auto final=std::get<FlowNormalKinds>(CheckedFlowNormalKinds(t,model.flow,live,f.Input()));
  Check(final.expressions[result.value].IsFiniteNumber()==(edit==RawClosureEdit::kNone));
  Check(final.expressions[caught_read.value].IsFiniteNumber()==(edit==RawClosureEdit::kNone));
  Check(model.expressions.kinds==final.expressions);
  if(edit==RawClosureEdit::kNone&&copies==1) {
    Check(model.flow.original_exceptions.size()==1&&model.flow.original_exceptions[0]);
    auto gap=f.original;gap.operations[1].abrupt.reset();
    const RecoveryExpressionInput incomplete{f.plan,f.equations,f.order,&gap};
    const auto unknown=std::get<RecoveryProjection>(PrepareRecoveryProjection(t,projection,incomplete));
    Check(FindOriginalPhase(unknown.flow,{2},analysis::ExceptionPhase::kCatch).has_value());
    Check(OriginalCatchKinds(unknown.flow,*gap.catches[2]).IsEmpty());
    Check(unknown.expressions.kinds[caught_read.value].IsNumber()&&!unknown.expressions.kinds[caught_read.value].IsFiniteNumber());
    const auto unknown_live=std::get<FlowLiveness>(AnalyzeFlowLiveness(unknown.flow));
    const auto independently=std::get<FlowNormalKinds>(CheckedFlowNormalKinds(t,unknown.flow,unknown_live,incomplete));
    Check(independently.expressions==unknown.expressions.kinds);
    // A source-only cleanup is an explicit precision gap even when the
    // selected catch stays the same. The conservative completion graph has
    // additional routes; it cannot borrow the raw region's pending-value cap.
    auto cleanup=t;auto cleanup_plan=f.plan;auto cleanup_original=f.original;
    const ScopeId cleanup_scope{static_cast<unsigned>(cleanup.scopes.size())};
    const StmtId finalizer{static_cast<unsigned>(cleanup.statements.size())};
    cleanup.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});
    cleanup.statements.push_back({BlockStmt{cleanup_scope,0,0},{0}});
    std::get<TryStmt>(cleanup.statements[attempt.value].statement).finalizer=finalizer;
    cleanup_plan.omitted.resize(cleanup.statements.size());cleanup_plan.statements.resize(cleanup.statements.size());
    cleanup_original.statements=cleanup.statements.size();
    auto cleanup_projection=projection;cleanup_projection.omitted=cleanup_plan.omitted;
    cleanup_projection.initializers=cleanup_plan.statements;
    const RecoveryExpressionInput cleanup_input{cleanup_plan,f.equations,f.order,&cleanup_original};
    Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(cleanup)));
    const auto conservative=std::get<RecoveryProjection>(PrepareRecoveryProjection(cleanup,cleanup_projection,cleanup_input));
    Check(FindOriginalPhase(conservative.flow,{2},analysis::ExceptionPhase::kCatch).has_value());
    Check(!conservative.flow.original_exceptions[0]);
    Check(OriginalCatchKinds(conservative.flow,*cleanup_original.catches[2]).IsEmpty());
    // Change only the successful binding commit's provenance: its incoming
    // pending value is unchanged. Withdrawal must replay that contribution,
    // even when no value-domain change can wake the incoming edge again.
    auto changed_commit=model.flow;
    for(const auto& definition:changed_commit.definitions)
      if(definition.kind==FlowDefinitionKind::kCatch)
        changed_commit.events[definition.point.value].provenance=FlowProvenance::kChanged;
    const auto withdrawn=std::get<FlowNormalKinds>(CheckedFlowNormalKinds(t,changed_commit,live,f.Input()));
    Check(withdrawn.expressions[caught_read.value].IsNumber());
    Check(!withdrawn.expressions[caught_read.value].IsFiniteNumber());
  }
  return {model.work,model.allocated_bytes};
}
void ValueKindIntersection() {
  using K=analysis::ValueKinds;
  std::vector<K> domains{K{},K::Language(),K::Defined(),K::String(),K::Boolean(),K::ZeroOrOnePrimitive(),
    K::Number(),K::NonNaNNumber(),K::FiniteNumber(),K::FiniteNonzeroNumber(),K::Hole(),K::Environment(),K::OtherInternal(),K::Unknown()};
  for(auto numeric:{K::NonNaNNumber(),K::FiniteNumber(),K::FiniteNonzeroNumber()}) {
    domains.push_back(numeric.WithoutInt32MinTag());domains.push_back(numeric.WithIntegralNumber());
    domains.push_back(numeric.WithIntegralNumber().WithoutInt32MinTag());
  }
  for(auto internal:{K::Hole(),K::Environment(),K::OtherInternal()})domains.push_back(K::Language().Join(internal));
  Check(K::Boolean().Meet(K::Number()).IsEmpty());
  Check(K::Unknown().Meet(K::Number())==K::Number());
  Check(K::FiniteNumber().WithoutInt32MinTag().Meet(K::FiniteNumber().WithIntegralNumber())==
      K::FiniteNumber().WithoutInt32MinTag().WithIntegralNumber());
  for(auto a:domains)for(auto b:domains) {
    Check(a.Meet(b)==b.Meet(a));Check(a.Meet(a)==a);
    Check(a.Meet(b).Join(a)==a);Check(a.Meet(b).Join(b)==b);
    Check(a.Meet(a.Join(b))==a);Check(a.Join(a.Meet(b))==a);
    for(auto c:domains) {
      Check(a.Meet(b).Meet(c)==a.Meet(b.Meet(c)));
      if(a.Join(b)==b)Check(a.Meet(c).Join(b.Meet(c))==b.Meet(c));
    }
  }
}
// A retained raw result can refine a source join whose unused branch has a
// different category. Partially solved operands must not leak an incompatible
// category before the other branch is visited (arena order is not semantics).
void RawConditionalClosure(bool reverse) {
  Fixture f(0,false);auto& t=f.tree;
  t.expressions={
    {TreeLiteral{BooleanLiteral{true}},{0}},
    {SymbolExpr{{0}},{0}},
    {TreeLiteral{BooleanLiteral{false}},{0}},
    {ConditionalExpr{{0},{1},{2}},{0}}};
  f.plan.primitives.push_back(BooleanLiteral{true});f.plan.primitives.push_back(BooleanLiteral{false});
  f.equations={FlowPrimitiveReference{{2}},FlowBindingValue{{0}},FlowPrimitiveReference{{3}},FlowChoiceValue{{0},{1},{2}}};
  f.order={{0},{1},{2},{3}};
  t.statements.push_back({ReturnStmt{ExprId{3}},{0}});t.children={{1}};
  std::get<BlockStmt>(t.statements[0].statement).child_count=1;
  f.plan.read_sources.resize(4);f.plan.read_literals.resize(4);f.plan.replacements.resize(4);f.plan.transports.resize(4);
  f.plan.omitted.resize(2);f.plan.statements.resize(2);f.original.statements=2;f.original.input_ranges.resize(4);
  f.original.values.push_back({analysis::EntryDefinition{analysis::ParameterEntry{0}},analysis::ValueKinds::Number()});
  f.original.values.push_back({analysis::OpResult{{0},{}},analysis::ValueKinds::Number()});
  ExprId result{3};
  if(reverse) {
    t.expressions[3].expression=ConditionalExpr{{3},{2},{1}};
    f.equations[3]=FlowChoiceValue{{3},{2},{1}};
    std::reverse(t.expressions.begin(),t.expressions.end());std::reverse(f.equations.begin(),f.equations.end());
    for(auto& id:f.order)id.value=3-id.value;
    result={0};std::get<ReturnStmt>(t.statements[1].statement).value=result;
  }
  f.original.operations.push_back({{0},{0},0,analysis::ValueId{1},analysis::LanguageEffect::kNone,analysis::LanguageEffect::kNone,analysis::ExceptionalCompletion::kImpossible,analysis::ValueLife::kLive,{0},result,{},result});
  std::vector<std::uint8_t> stable{1};auto projection=f.Projection();projection.stable_entries=stable;
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(t)));
  const auto model=std::get<RecoveryProjection>(PrepareRecoveryProjection(t,projection,f.Input()));
  Check(model.expressions.kinds[result.value].IsNumber());
  const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(model.flow));
  const auto final=std::get<FlowNormalKinds>(CheckedFlowNormalKinds(t,model.flow,live,f.Input()));
  Check(final.expressions[result.value].IsNumber());
}
void ReadPhaseInitialization(bool reverse,bool early,bool emit=false) {
  Fixture f(1,reverse);auto& tree=f.tree;
  const ExprId update{static_cast<unsigned>(tree.expressions.size())};
  tree.expressions.push_back({UpdateExpr{{1},UpdateDirection::kIncrement,UpdateResult::kPostfix},{0}});
  f.equations.push_back(UnknownFlowValue{});f.order.push_back(update);
  const StmtId statement{static_cast<unsigned>(tree.statements.size())};
  tree.statements.push_back({ExpressionStmt{update},{0}});
  tree.children.insert(early?tree.children.begin():tree.children.end(),statement);
  std::get<BlockStmt>(tree.statements[0].statement).child_count=tree.children.size();
  const auto size=tree.expressions.size();
  f.plan.replacements.resize(size);f.plan.transports.resize(size);f.plan.read_sources.resize(size);f.plan.read_literals.resize(size);
  f.plan.omitted.resize(tree.statements.size());f.plan.statements.resize(tree.statements.size());
  f.original.statements=tree.statements.size();f.original.input_ranges.resize(size);
  const auto model=std::get<RecoveryProjection>(PrepareRecoveryProjection(tree,f.Projection(),f.Input()));
  bool seen=false,conversion=false;
  for(const auto& event:model.flow.events)if(event.expression==update) {
    bool throws=false;
    for(auto edge=event.outgoing;edge;edge=model.flow.edges[*edge].outgoing)
      throws|=model.flow.edges[*edge].completion==FlowCompletion::kThrow;
    if(event.kind==FlowEventKind::kRead){seen=true;Check(throws==early);}
    if(event.kind==FlowEventKind::kEvaluation){conversion=true;Check(throws==early);}
  }
  for(const auto& definition:model.flow.definitions)if(definition.kind==FlowDefinitionKind::kUpdate) {
    const auto& commit=model.flow.events[definition.point.value];
    Check(commit.effect==ExpressionEvaluation::kRequired);
    Check(commit.discarded&&model.flow.discarded[commit.discarded->value].effect==ExpressionEvaluation::kRequired);
  }
  Check(seen&&conversion);
  if(emit) {
    std::ostringstream output;Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output,{}, {},nullptr,&f.original)));
    std::cout<<nlohmann::json{{"source",output.str()},{"reverse",reverse},{"early",early}}<<'\n';
  }
}
void CapturedNumericKinds(bool reverse,unsigned form) {
  Fixture f(1,reverse);auto& tree=f.tree;
  const auto unknown=std::get<UnaryExpr>(tree.expressions[f.values[0].value].expression).operand;
  const ExprId update{static_cast<unsigned>(tree.expressions.size())};tree.expressions.push_back({UpdateExpr{{1},UpdateDirection::kIncrement,UpdateResult::kPostfix},{0}});
  f.equations.push_back(UnknownFlowValue{});f.order.push_back(update);
  f.plan.replacements.resize(tree.expressions.size());f.plan.transports.resize(tree.expressions.size());
  f.plan.read_sources.resize(tree.expressions.size());f.plan.read_literals.resize(tree.expressions.size());
  f.original.input_ranges.resize(tree.expressions.size());
  RecoveryFlow flow{};flow.original=&f.original;flow.events.resize(5);flow.bindings.resize(2);
  flow.bindings[1].exposure=FlowBindingExposure::kPrivate;
  const auto point=[&](unsigned i){return FlowPoint{reverse?4-i:i};};flow.entry=point(0);
  flow.definitions={{point(0),{1},FlowDefinitionKind::kInitializer,{},form==0?f.values[0]:unknown},
      {point(2),{1},FlowDefinitionKind::kAssignment,{},form==0?unknown:f.values[0]},
      {point(3),{1},FlowDefinitionKind::kUpdate,{},update,{},update}};
  if(form!=3)flow.definitions[2].input=FlowCapturedValue{point(1),{1}};
  for(unsigned i=0;i<3;++i)flow.events[flow.definitions[i].point.value].definition=FlowDefinitionId{i};
  flow.events[point(1).value].read=SymbolId{1};flow.events[point(4).value].read=SymbolId{1};
  flow.references={{point(1),{1},FlowReferenceKind::kRead,update},{point(4),{1},FlowReferenceKind::kRead,f.reads[0]}};
  const auto edge=[&](unsigned a,unsigned b,FlowCompletion completion=FlowCompletion::kNormal) {
    const auto from=point(a),to=point(b);flow.edges.push_back({from,to,completion,flow.events[to.value].incoming,flow.events[from.value].outgoing});
    flow.events[to.value].incoming=flow.events[from.value].outgoing=flow.edges.size()-1;
  };
  edge(0,1);edge(1,2);if(form==2)edge(2,4,FlowCompletion::kThrow);else {edge(2,3);edge(3,4);}
  const auto live=std::get<FlowLiveness>(AnalyzeFlowLiveness(flow));
  const auto actual=std::get<FlowNormalKinds>(CheckedFlowNormalKinds(tree,flow,live,f.Input()));
  // Capture Number then overwrite with unknown, or the converse. A failed
  // conversion exposes the intervening write; success uses the old capture.
  Check(actual.expressions[f.reads[0].value]==((form==0||form==2)?analysis::ValueKinds::Number():analysis::ValueKinds::Language()));
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
  const auto solve=[&]{return std::get<FlowNormalKinds>(CheckedFlowNormalKinds(f.tree,flow,std::get<FlowLiveness>(AnalyzeFlowLiveness(flow)),f.Input()));};
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
  const auto actual=std::get<FlowNormalKinds>(CheckedFlowNormalKinds(fixture.tree,projection.flow,live,fixture.Input()));
  Check(actual.expressions[fixture.reads[0].value]==analysis::ValueKinds::Language());
}
void ScopedExposedKinds(bool reverse,CellVisibility visibility) {
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
  f.original.statements=f.tree.statements.size();f.original.input_ranges.resize(f.tree.expressions.size());
  auto model=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input()));
  Check(model.flow.bindings[1].cell_access==CellVisibility::kClosed);
  model.flow.bindings[1].cell_access=visibility;
  const auto after_callback=visibility==CellVisibility::kClosed?analysis::ValueKinds::Number():analysis::ValueKinds::Language();
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
  const auto changed=std::get<FlowNormalKinds>(CheckedFlowNormalKinds(f.tree,model.flow,live,f.Input()));
  Check(changed.expressions[f.reads[0].value]==after_callback);
  event.effect=ExpressionEvaluation::kInert;
  Check(std::get<FlowNormalKinds>(CheckedFlowNormalKinds(f.tree,model.flow,live,f.Input())).expressions[f.reads[0].value].IsNumber());
  // Independent two-completion counterexample: the same retained operation
  // has no normal callback but may invoke one before throwing. This model
  // graph is not a certificate for a changed canonical source tree.
  event.effect=ExpressionEvaluation::kRequired;
  event.effects=SourceEffects{analysis::LanguageEffect::kNone,analysis::LanguageEffect::kUserCode,
      analysis::ExceptionalCompletion::kMayThrow};
  Check(std::get<FlowNormalKinds>(CheckedFlowNormalKinds(f.tree,model.flow,live,f.Input())).expressions[f.reads[0].value].IsNumber());
  auto& completion=model.flow.edges[*model.flow.events[read->point.value].incoming].completion;
  completion=FlowCompletion::kThrow;
  Check(std::get<FlowNormalKinds>(CheckedFlowNormalKinds(f.tree,model.flow,live,f.Input())).expressions[f.reads[0].value]==after_callback);
  event.effects=SourceEffects{analysis::LanguageEffect::kUserCode,analysis::LanguageEffect::kNone,
      analysis::ExceptionalCompletion::kMayThrow};
  Check(std::get<FlowNormalKinds>(CheckedFlowNormalKinds(f.tree,model.flow,live,f.Input())).expressions[f.reads[0].value].IsNumber());
  completion=FlowCompletion::kNormal;
  Check(std::get<FlowNormalKinds>(CheckedFlowNormalKinds(f.tree,model.flow,live,f.Input())).expressions[f.reads[0].value]==after_callback);
}
std::pair<std::uint64_t,std::size_t> ScopedPureWriteKinds(bool reverse,bool emit=false,unsigned count=1) {
  Fixture f(count,reverse);
  for(unsigned i=1;i<f.tree.symbols.size();++i) {
    f.private_bindings[i]=0;f.tree.symbols[i].kind=BindingKind::kLocal;f.tree.symbols[i].temporary.reset();
    const auto number=std::to_string(i);auto& name=f.tree.names[f.tree.symbols[i].name.value];
    name=u"saved";name.append(number.begin(),number.end());
  }
  const SymbolId other{static_cast<unsigned>(f.tree.symbols.size())};
  const NameId name{static_cast<unsigned>(f.tree.names.size())};f.tree.names.push_back(u"other");
  f.tree.symbols.push_back({{1},name,BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
  const auto expression=[&](TreeExpression node,FlowValueEquation equation) {
    const ExprId id{static_cast<unsigned>(f.tree.expressions.size())};
    f.tree.expressions.push_back({std::move(node),{0}});f.equations.push_back(equation);f.order.push_back(id);return id;
  };
  const auto zero=expression(TreeLiteral{IntegerLiteral{7}},FlowPrimitiveReference{{1}});
  const auto value=expression(TreeLiteral{IntegerLiteral{7}},FlowPrimitiveReference{{1}});
  const auto write=expression(AssignmentExpr{SymbolTarget{other},value},FlowExpressionValue{value});
  const StmtId declaration{static_cast<unsigned>(f.tree.statements.size())};
  f.tree.statements.push_back({DeclarationStmt{other,zero},{0}});
  const StmtId assignment{static_cast<unsigned>(f.tree.statements.size())};
  f.tree.statements.push_back({ExpressionStmt{write},{0}});
  f.tree.children.insert(f.tree.children.begin(),declaration);
  f.tree.children.insert(f.tree.children.begin()+3*count,assignment);
  const auto returned=expression(SymbolExpr{{count}},FlowBindingValue{{count}});
  const StmtId returning{static_cast<unsigned>(f.tree.statements.size())};
  f.tree.statements.push_back({ReturnStmt{returned},{0}});f.tree.children.push_back(returning);
  std::get<BlockStmt>(f.tree.statements[0].statement).child_count=f.tree.children.size();
  const auto b=f.tree.symbols.size(),e=f.tree.expressions.size(),s=f.tree.statements.size();
  f.plan.aliases.resize(b);f.plan.storage.resize(b);f.plan.read_bindings.resize(b);f.plan.literals.resize(b);f.private_bindings.resize(b,0);
  f.plan.read_sources.resize(e);f.plan.read_literals.resize(e);f.plan.replacements.resize(e);f.plan.transports.resize(e);
  f.plan.omitted.resize(s);f.plan.statements.resize(s);
  f.original.statements=s;f.original.bindings=b;f.original.input_ranges.resize(e);
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(f.tree)));
  const auto model=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input()));
  const analysis::SemanticBudget exact{model.allocated_bytes,model.work};
  Check(std::holds_alternative<RecoveryProjection>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input(),exact)));
  Check(std::get<TreeStructureError>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input(),{exact.bytes-1,exact.work}))==TreeStructureError::kResourceLimit);
  Check(std::get<TreeStructureError>(PrepareRecoveryProjection(f.tree,f.Projection(),f.Input(),{exact.bytes,exact.work-1}))==TreeStructureError::kWorkLimit);
  bool observable=false;
  for(const auto& d:model.flow.definitions)if(d.write==write) {
    Check(d.effect==FlowWriteEffect::kObservable);observable=true;
  }
  Check(observable);
  bool event_seen=false;
  for(const auto& event:model.flow.events)if(event.expression==write) {
    Check(event.effect==ExpressionEvaluation::kRequired&&event.effects);
    Check(!event.MayInvokeUserCode(FlowCompletion::kNormal));event_seen=true;
  }
  Check(event_seen);
  // Pure PutValue to another declarative unit must retain its observable write
  // without inventing a user callback that could replace this numeric value.
  for(unsigned i=0;i<count;++i) {
    Check(model.expressions.kinds[f.reads[i].value].IsNumber());
    Check(model.expressions.evaluations[f.results[i].value]==ExpressionEvaluation::kInert);
  }
  if(emit) {
    std::ostringstream output;
    Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(f.tree,output,{}, {},nullptr,&f.original)));
    std::cout<<nlohmann::json{{"source",output.str()},{"reverse",reverse},{"count",count},
      {"events",model.flow.events.size()},{"edges",model.flow.edges.size()},{"work",model.work},{"bytes",model.allocated_bytes}}<<'\n';
  }
  return {model.work,model.allocated_bytes};
}
// Cell reachability is canonical and independent of write observability,
// entry-domain certificates and source layout. Current and candidate projections
// borrow the same unchanged owner; a different owner cannot reuse this proof.
std::pair<std::uint64_t,std::size_t> CanonicalCells(unsigned count,bool reverse) {
  Fixture f(count,reverse);
  for(unsigned i=1;i<f.tree.symbols.size();++i) {
    f.tree.symbols[i].kind=i%2?BindingKind::kLocal:BindingKind::kCatchParameter;
    f.tree.symbols[i].temporary.reset();
    if(i%3==0)f.tree.closure_captures.push_back({i});
  }
  const ExprId creation{static_cast<unsigned>(f.tree.expressions.size())};
  f.tree.expressions.push_back({FunctionExpr{{1},{0,static_cast<unsigned>(f.tree.closure_captures.size())}},{0}});
  TreeCheckBudget budget({});const auto cells=AnalyzeSourceCells(f.tree,budget);
  Check(cells.owner==&f.tree&&!cells.mapped_arguments);
  Check(cells.access[0]==CellVisibility::kClosed);
  for(unsigned i=1;i<f.tree.symbols.size();++i)
    Check(cells.access[i]==(i%3==0?CellVisibility::kExternal:CellVisibility::kClosed));
  Check(cells.Captured(creation,budget).size()==f.tree.closure_captures.size());
  Check(cells.Captured({0},budget).empty());
  const auto run=[&](analysis::SemanticBudget allowance) {
    try {TreeCheckBudget limited(allowance);const auto facts=AnalyzeSourceCells(f.tree,limited);
      Check(facts.Captured(creation,limited).size()==f.tree.closure_captures.size());
      Check(facts.Captured({0},limited).empty());return std::optional<TreeStructureError>{};
    } catch(TreeStructureError error){return std::optional{error};}
  };
  const analysis::SemanticBudget exact{budget.Bytes(),budget.Work()};
  Check(!run(exact));Check(run({exact.bytes-1,exact.work})==TreeStructureError::kResourceLimit);
  Check(run({exact.bytes,exact.work-1})==TreeStructureError::kWorkLimit);
  return {budget.Work(),budget.Bytes()};
}
void CellAccessContracts(bool reverse) {
  Fixture f(1,reverse);f.private_bindings[1]=0;
  auto projection=f.Projection();TreeCheckBudget budget({});
  const auto closed=AnalyzeSourceCells(f.tree,budget);projection.cells=&closed;
  auto current=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,projection,f.Input()));
  Check(current.flow.bindings[1].cell_access==CellVisibility::kClosed);
  Check(current.flow.bindings[1].exposure==FlowBindingExposure::kObservable);
  Check(!current.flow.bindings[0].stable_entry); // closure proof cannot grant entry types
  f.plan.omitted[f.declarations[0].value]=1;
  const auto trial=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,projection,f.Input()));
  Check(trial.flow.bindings[1].cell_access==current.flow.bindings[1].cell_access);
  Check(!trial.initialized_reads[f.reads[0].value]);
  f.plan.omitted[f.declarations[0].value]=0;
  Fixture other(1,reverse);auto wrong=other.Projection();wrong.cells=&closed;
  Check(std::get<TreeStructureError>(PrepareRecoveryProjection(other.tree,wrong,other.Input()))==TreeStructureError::kOwnership);
  f.tree.closure_captures={{1}};const auto captured=AnalyzeSourceCells(f.tree,budget);
  projection.cells=&captured;
  f.plan.storage[1]=SymbolId{0};
  const auto shared=std::get<RecoveryProjection>(PrepareRecoveryProjection(f.tree,projection,f.Input()));
  Check(shared.flow.bindings[0].cell_access==CellVisibility::kExternal);
  Check(shared.flow.bindings[1].cell_access==CellVisibility::kExternal);
  f.tree.strictness=Strictness::kSloppy;
  Check(AnalyzeSourceCells(f.tree,budget).access[0]==CellVisibility::kExternal);
  f.tree.callable=CallableKind::kArrow;
  Check(AnalyzeSourceCells(f.tree,budget).access[0]==CellVisibility::kClosed);
  f.tree.callable=CallableKind::kFunction;f.tree.parameters[0].rest=true;
  Check(AnalyzeSourceCells(f.tree,budget).access[0]==CellVisibility::kClosed);
  f.tree.expressions.push_back({ClassExpr{std::make_shared<ClassTree>(),{}},{0}});
  const auto incomplete=AnalyzeSourceCells(f.tree,budget);
  Check(incomplete.access[0]==CellVisibility::kUnmodeled&&incomplete.access[1]==CellVisibility::kExternal);
  f.tree.expressions.pop_back();
  f.tree.recovery_issues.push_back({RecoveryReason::kUnsupportedControl,abc::CodeOffset{1},abc::CodeOffset{2}});
  Check(AnalyzeSourceCells(f.tree,budget).access[0]==CellVisibility::kUnmodeled);
  f.tree.symbols[0].kind=BindingKind::kModule;
  Check(AnalyzeSourceCells(f.tree,budget).access[0]==CellVisibility::kExternal);
  f.tree.recovery_issues.clear();f.tree.symbols[0].kind=BindingKind::kParameter;
  f.tree.modules.push_back({{0},{{},{0}},ModuleAccess::kReadOnly});
  Check(AnalyzeSourceCells(f.tree,budget).access[0]==CellVisibility::kExternal);
}
void SourceEffectPhases() {
  using L=analysis::LanguageEffect;
  const std::array unknown{analysis::ValueKinds::Language()};
  const std::array primitive{analysis::ValueKinds::Number()};
  for(const auto resources:{ResourceFailureScope::kObservable,ResourceFailureScope::kExcluded}) {
    const auto receiver=NormalPropertyReceiverEffects({0},unknown,resources);
    Check(receiver.Evaluation()==ExpressionEvaluation::kRequired);
    Check(!HasLanguageEffect(receiver.normal,L::kUserCode)&&!HasLanguageEffect(receiver.before_throw,L::kUserCode));
    const auto key=NormalPropertyKeyEffects(ComputedKey{{0}},unknown,resources);
    Check(HasLanguageEffect(key.normal,L::kUserCode)&&HasLanguageEffect(key.before_throw,L::kUserCode));
    const auto allocation=NormalValueEffects(EmptyObjectExpr{},unknown,resources);
    Check(allocation.Evaluation()==ExpressionEvaluation::kRequired&&!HasLanguageEffect(allocation.normal,L::kUserCode));
    const auto negate=NormalValueEffects(UnaryExpr{TreeUnary::kNegate,{0}},primitive,resources);
    Check(negate.Evaluation()==ExpressionEvaluation::kInert);
    const auto hooks=NormalValueEffects(UnaryExpr{TreeUnary::kNegate,{0}},unknown,resources);
    Check(HasLanguageEffect(hooks.normal,L::kUserCode)&&HasLanguageEffect(hooks.before_throw,L::kUserCode));
    const auto store=NormalValueEffects(AssignmentExpr{SymbolTarget{{0}},{0}},unknown,resources);
    Check(store.Evaluation()==ExpressionEvaluation::kRequired&&!HasLanguageEffect(store.normal,L::kUserCode));
    const auto put=NormalValueEffects(AssignmentExpr{MemberTarget{{0},ComputedKey{{0}}},{0}},unknown,resources);
    Check(HasLanguageEffect(put.normal,L::kUserCode)&&HasLanguageEffect(put.before_throw,L::kUserCode));
    const auto key_number=NormalPropertyKeyEffects(ComputedKey{{0}},primitive,resources);
    Check(!HasLanguageEffect(key_number.normal,L::kUserCode));
    Check((key_number.Evaluation()==ExpressionEvaluation::kInert)==(resources==ResourceFailureScope::kExcluded));
  }
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
  if(argc==2&&std::string_view(argv[1])=="--control-paths") {
    for(unsigned form=0;form<10;++form)for(bool original:{false,true})for(bool reverse:{false,true})for(bool split:{false,true})
      ControlCompletions(form,reverse,split,0,original,true);
    for(unsigned form:{4U,5U})for(bool reverse:{false,true})ControlCompletions(form,reverse,false,1,false,true);
    for(unsigned copies:{200U,201U,400U,800U})for(bool reverse:{false,true})
      ControlCompletions(0,reverse,false,copies,false,true);
    return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--closure-raw") {
    ValueKindIntersection();RawConditionalClosure(false);RawConditionalClosure(true);
    for(bool reverse:{false,true})for(bool changed:{false,true})for(auto edit:{RawClosureEdit::kNone,RawClosureEdit::kUnknownRead,RawClosureEdit::kChangedThrowPath})RawProjectionClosure(changed,edit,reverse);
    for(bool reverse:{false,true}) {
      std::pair<std::uint64_t,std::size_t> prior{};
      for(unsigned count:{200U,201U,400U,800U}) {
        const auto cost=RawProjectionClosure(true,RawClosureEdit::kNone,reverse,count);
        if(prior.first){Check(cost.first<prior.first*3);Check(cost.second<prior.second*3);}prior=cost;
        std::cout<<"raw closure count="<<count<<" reverse="<<reverse<<" work="<<cost.first<<" bytes="<<cost.second<<'\n';
      }
    }
    return 0;
  }

  if(argc==2&&std::string_view(argv[1])=="--closure-init") {
    for(bool original:{false,true})for(bool reverse:{false,true})for(bool known:{false,true})ProjectionClosure(1,known,reverse,false,true,original);
    return 0;
  }

  if(argc==2&&std::string_view(argv[1])=="--closure") {
    for(bool reverse:{false,true})for(bool known:{false,true}) {
      for(unsigned depth:{1U,4U})ProjectionClosure(depth,known,reverse);
      std::pair<std::uint64_t,std::size_t> prior{};
      for(unsigned depth:{200U,201U,400U,800U}) {
        const auto cost=ProjectionClosure(depth,known,reverse);
        if(prior.first){Check(cost.first<prior.first*3);Check(cost.second<prior.second*3);}
        std::cout<<"closure depth="<<depth<<" known="<<known<<" reverse="<<reverse<<" work="<<cost.first<<" bytes="<<cost.second<<'\n';
        prior=cost;
      }
    }
    return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--closure-source") {
    for(bool original:{false,true})for(bool reverse:{false,true})for(bool known:{false,true})for(unsigned depth:{1U,4U})ProjectionClosure(depth,known,reverse,true,false,original);
    return 0;
  }

  if(argc==2&&std::string_view(argv[1])=="--publication") {
    for(bool before:{false,true})for(bool reverse:{false,true})for(unsigned count:{0U,1U,200U,201U,400U,800U})JointProjectionSource(reverse,count,before,true);
    return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--read-phases") {
    for(bool reverse:{false,true})for(bool early:{false,true})ReadPhaseInitialization(reverse,early,true);
    return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--updates") {
    for(bool reverse:{false,true})for(bool early:{false,true})ReadPhaseInitialization(reverse,early);
    for(bool reverse:{false,true})for(bool unknown:{false,true})CapturedEffectClosure(reverse,unknown);
    for(bool caught:{false,true})for(bool reverse:{false,true})for(bool numeric:{false,true})for(bool increment:{false,true})for(bool prefix:{false,true})UpdateSource(reverse,numeric,increment,prefix,true,caught);
    return 0;
  }
  for(bool reverse:{false,true})for(bool early:{false,true})ReadPhaseInitialization(reverse,early);
  for(bool reverse:{false,true})for(unsigned form=0;form<4;++form)CapturedNumericKinds(reverse,form);
  if(argc==2&&std::string_view(argv[1])=="--unit-source") {
    for(bool update:{false,true})for(bool captured:{false,true})for(bool reverse:{false,true})for(unsigned count:{0U,1U,200U,201U,400U,800U})CurrentUnitSource(captured,count,reverse,true,update);
    return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--unit-kinds") {
    UnitCapabilityContract();
    for(unsigned form=0;form<10;++form)for(bool loop:{false,true})for(bool reverse:{false,true})CurrentUnitKinds(reverse,loop,form);
    return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--effects") {
    for(bool reverse:{false,true})ScopedPureWriteKinds(reverse,true);
    return 0;
  }
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
  for(unsigned form=0;form<10;++form)for(bool original:{false,true})for(bool reverse:{false,true})for(bool split:{false,true})
    ControlCompletions(form,reverse,split,0,original);
  for(unsigned form:{4U,5U})for(bool reverse:{false,true})ControlCompletions(form,reverse,false,1,false);
  std::pair<std::uint64_t,std::size_t> control_cost{};
  for(unsigned copies:{200U,201U,400U,800U}) {
    const auto cost=ControlCompletions(0,false,false,copies,false);Check(cost==ControlCompletions(0,true,false,copies,false));
    if(control_cost.first){const double ratio=copies==201?1.1:2.5;Check(cost.first<control_cost.first*ratio);Check(cost.second<control_cost.second*ratio);}
    control_cost=cost;std::cout<<"control copies="<<copies<<" work="<<cost.first<<" bytes="<<cost.second<<'\n';
  }
  SparseStateBudgets();
  for(bool reverse:{false,true})for(unsigned hops:{1U,4U})SparseCompletionGuard(reverse,hops);
  CapturedKinds(false);CapturedKinds(true);ActivationKinds(false);ActivationKinds(true);
  for(const auto visibility:{CellVisibility::kClosed,CellVisibility::kExternal,CellVisibility::kUnmodeled})
    for(bool reverse:{false,true})ScopedExposedKinds(reverse,visibility);
  ScopedPureWriteKinds(false);ScopedPureWriteKinds(true);
  SourceEffectPhases();
  CellAccessContracts(false);CellAccessContracts(true);
  std::pair<std::uint64_t,std::size_t> cells_previous;
  CanonicalCells(1,false);CanonicalCells(1,true);
  for(unsigned count:{200U,201U,400U,800U})for(bool reverse:{false,true}) {
    const auto measured=CanonicalCells(count,reverse);
    if(!reverse) {
      Check(!cells_previous.first||measured.first<cells_previous.first*3);
      Check(!cells_previous.second||measured.second<cells_previous.second*3);
      cells_previous=measured;
      std::cout<<"canonical cells count="<<count<<" work="<<measured.first<<" bytes="<<measured.second<<'\n';
    }
  }
  std::uint64_t effects_work=0;std::size_t effects_bytes=0;
  for(unsigned count:{200U,201U,400U,800U}) {
    const auto [work,bytes]=ScopedPureWriteKinds(false,false,count);ScopedPureWriteKinds(true,false,count);
    Check(!effects_work||work<effects_work*3);Check(!effects_bytes||bytes<effects_bytes*3);
    effects_work=work;effects_bytes=bytes;
    std::cout<<"completion effects count="<<count<<" work="<<work<<" bytes="<<bytes<<'\n';
  }
  ActivationSource(0,false,false);ActivationSource(1,true,false);
  for(bool before:{false,true})for(bool reverse:{false,true}) {
    std::pair<std::uint64_t,std::size_t> previous;
    for(unsigned count:{0U,1U,200U,201U,400U,800U}) {
      const auto current=JointProjectionSource(reverse,count,before,false);
      if(previous.first&&count>=201){Check(current.first<previous.first*3);Check(current.second<previous.second*3);}
      previous=current;
    }
  }
  for(bool reverse:{false,true})for(bool known:{false,true}) {
    for(unsigned depth:{1U,4U})ProjectionClosure(depth,known,reverse);
    std::pair<std::uint64_t,std::size_t> prior{};
    for(unsigned depth:{200U,201U,400U,800U}) {
      const auto cost=ProjectionClosure(depth,known,reverse);
      if(prior.first){Check(cost.first<prior.first*3);Check(cost.second<prior.second*3);}prior=cost;
    }
  }
  for(bool original:{false,true})for(bool reverse:{false,true})for(bool known:{false,true}) {
    ProjectionClosure(1,known,reverse,false,true,original);
    if(!original)for(unsigned depth:{1U,4U,200U,201U,400U,800U})ProjectionClosure(depth,known,reverse,false,false,original);
  }
  ValueKindIntersection();RawConditionalClosure(false);RawConditionalClosure(true);
  for(bool reverse:{false,true})for(bool changed:{false,true})for(auto edit:{RawClosureEdit::kNone,RawClosureEdit::kUnknownRead,RawClosureEdit::kChangedThrowPath})RawProjectionClosure(changed,edit,reverse);
  for(bool reverse:{false,true}) {
    std::pair<std::uint64_t,std::size_t> prior{};
    for(unsigned count:{200U,201U,400U,800U}) {
      const auto cost=RawProjectionClosure(true,RawClosureEdit::kNone,reverse,count);
      if(prior.first){Check(cost.first<prior.first*3);Check(cost.second<prior.second*3);}prior=cost;
    }
  }
  OriginalControlKinds(false);OriginalControlKinds(true);
  for(bool reverse:{false,true})for(unsigned copies:{1U,4U,200U,201U}) {
    const auto measured=OriginalControlKinds(reverse,1,copies);
    std::cout<<"control ready copies="<<copies<<" reverse="<<reverse<<" work="<<measured.first<<" bytes="<<measured.second<<'\n';
  }
  for(bool reverse:{false,true}) {
    std::pair<std::uint64_t,std::size_t> previous{};
    for(unsigned count:{200U,201U,400U,800U}) {
      const auto measured=OriginalControlKinds(reverse,count);
      Check(!previous.first||measured.first<previous.first*3);Check(!previous.second||measured.second<previous.second*3);
      previous=measured;
      std::cout<<"original controls count="<<count<<" reverse="<<reverse<<" work="<<measured.first<<" bytes="<<measured.second<<'\n';
    }
  }
  OriginalIntrinsicEffects(false);OriginalIntrinsicEffects(true);
  for(bool reverse:{false,true})for(bool unknown:{false,true})CapturedEffectClosure(reverse,unknown);
  for(bool reverse:{false,true}) {
    std::uint64_t previous=0;
    for(unsigned count:{200U,201U,400U,800U}) {
      const auto work=CapturedInputCosts(count,reverse);
      if(previous)Check(work<previous*3);previous=work;
    }
  }
  for(bool caught:{false,true})for(bool reverse:{false,true})for(bool numeric:{false,true})for(bool increment:{false,true})for(bool prefix:{false,true})UpdateSource(reverse,numeric,increment,prefix,false,caught);
  Case(false);Case(true);ChangedDefinition(false);ChangedDefinition(true);
  OriginalEntries(false);OriginalEntries(true);UnitCapabilityContract();
  for(unsigned form=0;form<10;++form)for(bool loop:{false,true})for(bool reverse:{false,true})CurrentUnitKinds(reverse,loop,form);
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
