#include "../../../src/source/binding_initializer.h"
#include "../../../src/source/loop_syntax.h"
#include "../../../src/source/nullish_syntax.h"
#include "../../../src/source/definition_elision.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
#include <nlohmann/json.hpp>
#include <sstream>
using namespace abcd;
using namespace abcd::source;
namespace {
struct Fixture {
  FunctionTree tree{};
  Fixture() {
    tree.callable=CallableKind::kFunction;tree.strictness=Strictness::kStrict;
    tree.parameter_scope={0};tree.body_scope={1};tree.body={0};
    tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
    tree.names={u"produce",u"flag",u"state",u"error"};tree.origins={OriginalSite{{0},{0}}};
    for(unsigned i=0;i<2;++i) {tree.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}});tree.parameters.push_back({{i},{},false});}
    tree.symbols.push_back({{1},{2},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
    tree.statements.push_back({BlockStmt{{1},0,0},{0}});
  }
  ExprId Expr(TreeExpression node) {ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};tree.expressions.push_back({std::move(node),{0}});return id;}
  StmtId Stmt(TreeStatement node) {StmtId id{static_cast<std::uint32_t>(tree.statements.size())};tree.statements.push_back({std::move(node),{0}});return id;}
  ExprId Read(unsigned symbol) {return Expr(SymbolExpr{{symbol}});}
  ExprId Call(unsigned value) {const auto arg=Expr(TreeLiteral{IntegerLiteral{static_cast<std::int32_t>(value)}});const auto first=static_cast<std::uint32_t>(tree.arguments.size());tree.arguments.push_back(arg);return Expr(CallExpr{Read(0),{first,1},CallReference::kValue});}
  ExprId Store(unsigned value) {return Expr(AssignmentExpr{SymbolTarget{{2}},Call(value)});}
  StmtId Block(std::vector<StmtId> body,ScopeId scope) {const auto first=static_cast<std::uint32_t>(tree.children.size());tree.children.insert(tree.children.end(),body.begin(),body.end());return Stmt(BlockStmt{scope,first,static_cast<std::uint32_t>(body.size())});}
  ScopeId Scope(ScopeKind kind=ScopeKind::kBlock) {const ScopeId id{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({kind,ScopeId{1}});return id;}
  void Finish(std::vector<StmtId> body,bool reverse) {
    auto& root=std::get<BlockStmt>(tree.statements[0].statement);root.first_child=tree.children.size();root.child_count=body.size();tree.children.insert(tree.children.end(),body.begin(),body.end());
    for(unsigned i=0;i<tree.statements.size();++i)if(const auto* block=std::get_if<BlockStmt>(&tree.statements[i].statement))for(unsigned j=0;j<=block->child_count;++j){const auto anchor=static_cast<std::uint32_t>(tree.anchors.size());tree.anchors.push_back({BlockBoundary{{i},j},{0}});tree.operations.push_back({{anchor},AnchorId{anchor}});}
    if(!reverse)return;
    const auto remap=[&](ExprId id){return ExprId{static_cast<std::uint32_t>(tree.expressions.size()-1-id.value)};};
    for(auto& expression:tree.expressions)std::visit([&](auto& node){using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,AssignmentExpr>)node.value=remap(node.value);
      else if constexpr(std::is_same_v<T,CallExpr>)node.callee=remap(node.callee);
      else if constexpr(std::is_same_v<T,ConditionalExpr>){node.condition=remap(node.condition);node.consequent=remap(node.consequent);node.alternate=remap(node.alternate);}
      else if constexpr(std::is_same_v<T,SequenceExpr>||std::is_same_v<T,LogicalExpr>){node.left=remap(node.left);node.right=remap(node.right);}
      else if constexpr(std::is_same_v<T,UnaryExpr>)node.operand=remap(node.operand);
    },expression.expression);
    for(auto& statement:tree.statements)std::visit([&](auto& node){using T=std::decay_t<decltype(node)>;
      if constexpr(std::is_same_v<T,DeclarationStmt>){if(node.initializer)node.initializer=remap(*node.initializer);}
      else if constexpr(std::is_same_v<T,ExpressionStmt>)node.expression=remap(node.expression);
      else if constexpr(std::is_same_v<T,ReturnStmt>){if(node.value)node.value=remap(*node.value);}
      else if constexpr(std::is_same_v<T,IfStmt>)node.condition=remap(node.condition);
    },statement.statement);
    for(auto& arg:tree.arguments)arg=remap(arg);std::reverse(tree.expressions.begin(),tree.expressions.end());
  }
};
std::uint64_t Check(unsigned form,unsigned count,bool reverse,bool emit,bool resources=false) {
  Fixture f;auto& tree=f.tree;
  if(resources)tree.identity.contract=SourceContract::kLanguageAndHostV1;
  std::vector<StmtId> body{f.Stmt(DeclarationStmt{{2},form>=5?std::nullopt:std::optional{f.Read(1)}})};
  if(form==6) {const auto arg=f.Read(2);const auto first=static_cast<std::uint32_t>(tree.arguments.size());tree.arguments.push_back(arg);body.push_back(f.Stmt(ExpressionStmt{f.Expr(CallExpr{f.Read(0),{first,1},CallReference::kValue})}));}
  for(unsigned i=0;i<count;++i)body.push_back(f.Stmt(ExpressionStmt{f.Call(0)}));
  const auto first=f.Store(1);body.push_back(f.Stmt(form==4?TreeStatement{ReturnStmt{first}}:TreeStatement{ExpressionStmt{first}}));
  if(form==1) {
    const auto second=f.Stmt(ExpressionStmt{f.Store(2)});
    const auto scope=f.Scope(ScopeKind::kCatch);
    tree.symbols.push_back({scope,{3},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
    const auto handler=f.Block({f.Stmt(ReturnStmt{f.Read(2)})},scope);
    body.push_back(f.Stmt(TryStmt{f.Block({second},f.Scope()),CatchClause{{3},handler},{}}));
  } else if(form==2||form==3) {
    const auto second=f.Stmt(ExpressionStmt{f.Store(2)});
    std::optional<StmtId> alternate;
    if(form==3)alternate=f.Block({f.Stmt(ExpressionStmt{f.Store(3)})},f.Scope());
    body.push_back(f.Stmt(IfStmt{f.Read(1),f.Block({second},f.Scope()),alternate}));
  } else if(form!=4)body.push_back(f.Stmt(ExpressionStmt{f.Store(2)}));
  if(form!=4)body.push_back(f.Stmt(ReturnStmt{f.Read(2)}));
  f.Finish(body,reverse);
  const auto verified=VerifyTreeStructure(tree);
  if(const auto* error=std::get_if<TreeStructureError>(&verified))std::cerr<<"form="<<form<<" count="<<count<<" reverse="<<reverse<<" structure="<<static_cast<unsigned>(*error)<<'\n';
  test::Check(std::holds_alternative<TreeStructure>(verified));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto planned=PlanBindingInitializers(tree,loops,nullish);test::Check(std::holds_alternative<BindingInitializers>(planned));const auto& plan=std::get<BindingInitializers>(planned);
  const auto id=reverse?tree.expressions.size()-1-first.value:first.value;
  test::Check(plan.replacements[id].has_value()==(!resources&&(form==0||form==3||form==4||form==5||form==6)));
  // Existing lexical initialization stays when its value or binding is needed.
  // A dead assignment must not be promoted into a protected initializer first.
  if(form==5) {test::Check(plan.omitted[body[0].value]);test::Check(plan.statements[body[count+1].value].has_value()==resources);}
  else test::Check(!plan.omitted[body[0].value]);
  if(count<=1) {
    test::Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes,plan.work})));
    test::Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes-1,plan.work}))==TreeStructureError::kResourceLimit);
    test::Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes,plan.work-1}))==TreeStructureError::kWorkLimit);
  }
  std::ostringstream out;test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,out)));
  if(emit)std::cout<<nlohmann::json{{"source",out.str()},{"form",form},{"count",count}}<<'\n';
  return plan.work;
}
std::uint64_t Chain(unsigned size,bool cycle,bool observed,bool reverse,bool emit) {
  Fixture f;auto& tree=f.tree;
  for(unsigned i=1;i<size;++i) {
    const auto suffix=std::to_string(i);const auto name=static_cast<std::uint32_t>(tree.names.size());
    tree.names.push_back(u"slot"+std::u16string(suffix.begin(),suffix.end()));
    tree.symbols.push_back({{1},{name},BindingKind::kCompilerTemporary,DeclarationKind::kLet,
        NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
  }
  std::vector<StmtId> body,writes;
  for(unsigned i=0;i<size;++i)body.push_back(f.Stmt(DeclarationStmt{{i+2},
      i+1==size?f.Read(0):f.Expr(TreeLiteral{IntegerLiteral{static_cast<std::int32_t>(i)}})}));
  for(unsigned i=0;i<size;++i)writes.push_back(f.Stmt(ExpressionStmt{f.Expr(AssignmentExpr{
      SymbolTarget{{i+2}},f.Read(i?i+1:cycle?size+1:0)})}));
  if(cycle) {
    const auto loop=f.Stmt(LoopStmt{});const auto scope=f.Scope(),branch=f.Scope();tree.scopes[branch.value].parent=scope;
    auto inside=writes;
    inside.push_back(f.Stmt(IfStmt{f.Read(1),f.Block({f.Stmt(BreakStmt{loop})},branch),{}}));
    inside.push_back(f.Stmt(ExpressionStmt{f.Expr(AssignmentExpr{SymbolTarget{{1}},f.Expr(TreeLiteral{BooleanLiteral{true}})})}));
    std::get<LoopStmt>(tree.statements[loop.value].statement).body=f.Block(inside,scope);body.push_back(loop);
  } else body.insert(body.end(),writes.begin(),writes.end());
  const auto discarded=f.Stmt(ExpressionStmt{f.Read(size+1)});body.push_back(discarded);
  body.push_back(f.Stmt(ReturnStmt{observed?f.Read(size+1):f.Expr(TreeLiteral{IntegerLiteral{0}})}));
  f.Finish(body,reverse);test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto result=PlanBindingInitializers(tree,loops,nullish);test::Check(std::holds_alternative<BindingInitializers>(result));
  const auto& plan=std::get<BindingInitializers>(result);
  for(const auto statement:writes)test::Check(bool(plan.omitted[statement.value])==!observed);
  test::Check(plan.omitted[discarded.value]);
  if(size==2) {
    test::Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes,plan.work})));
    test::Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes-1,plan.work}))==TreeStructureError::kResourceLimit);
    test::Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes,plan.work-1}))==TreeStructureError::kWorkLimit);
  }
  std::ostringstream out;test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,out)));
  if(emit)std::cout<<nlohmann::json{{"source",out.str()},{"chain",true},{"size",size},{"cycle",cycle},{"observed",observed}}<<'\n';
  return plan.work;
}

std::uint64_t EmptyBinding(unsigned form,unsigned count,bool reverse,bool emit) {
  Fixture f;auto& tree=f.tree;
  if(form==5) {
    tree.symbols[2].kind=BindingKind::kLocal;tree.symbols[2].evidence=NameEvidence::kMetadata;
    tree.symbols[2].temporary.reset();
  }
  const auto declaration=f.Stmt(DeclarationStmt{{2},{}});
  std::vector<StmtId> body{declaration};
  if(form==2) {
    tree.arguments.push_back(f.Read(2));
    body.push_back(f.Stmt(ExpressionStmt{f.Expr(CallExpr{f.Read(0),{0,1},CallReference::kValue})}));
  }
  for(unsigned i=0;i<count;++i)body.push_back(f.Stmt(ExpressionStmt{f.Call(0)}));
  if(form==1||form==2) {
    const auto value=f.Store(1);const auto first=static_cast<std::uint32_t>(tree.arguments.size());
    tree.arguments.push_back(value);
    body.push_back(f.Stmt(ExpressionStmt{f.Expr(CallExpr{f.Read(0),{first,1},CallReference::kValue})}));
  }
  if(form==3)body.push_back(f.Stmt(ExpressionStmt{f.Expr(UpdateExpr{{2},UpdateDirection::kIncrement,UpdateResult::kPrefix})}));
  std::optional<StmtId> inner;
  if(form==4) {
    const auto scope=f.Scope();tree.symbols.push_back({scope,{2},BindingKind::kCompilerTemporary,
        DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
    inner=f.Stmt(DeclarationStmt{{3},{}});body.push_back(f.Block({*inner,f.Stmt(ExpressionStmt{f.Call(1)})},scope));
  }
  body.push_back(f.Stmt(ReturnStmt{form==3||form==4?f.Read(2):f.Read(1)}));
  f.Finish(body,reverse);test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto result=PlanBindingInitializers(tree,loops,nullish);test::Check(std::holds_alternative<BindingInitializers>(result));
  const auto& plan=std::get<BindingInitializers>(result);
  test::Check(bool(plan.omitted[declaration.value])==(form==0||form==1));
  if(inner)test::Check(plan.omitted[inner->value]);
  if(count==0) {
    test::Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes,plan.work})));
    test::Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes-1,plan.work}))==TreeStructureError::kResourceLimit);
    test::Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes,plan.work-1}))==TreeStructureError::kWorkLimit);
  }
  std::ostringstream out;test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,out)));
  if(emit)std::cout<<nlohmann::json{{"source",out.str()},{"binding",true},{"form",form},{"count",count}}<<'\n';
  return plan.work;
}

std::uint64_t DiscardedOperands(unsigned form,unsigned count,bool reverse,bool emit) {
  Fixture f;auto& tree=f.tree;
  const auto declaration=f.Stmt(DeclarationStmt{{2},f.Read(1)});
  const auto store=f.Store(1);auto result=f.Read(1);
  std::vector<ExprId> sequences;
  for(unsigned i=0;i<=count;++i) {
    auto left=f.Read(2);
    if(form)left=f.Expr(UnaryExpr{TreeUnary::kPlus,left});
    result=f.Expr(SequenceExpr{left,result});sequences.push_back(result);
  }
  f.Finish({declaration,f.Stmt(ExpressionStmt{store}),f.Stmt(ReturnStmt{result})},reverse);
  test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto planned=PlanBindingInitializers(tree,loops,nullish);test::Check(std::holds_alternative<BindingInitializers>(planned));
  const auto& plan=std::get<BindingInitializers>(planned);
  const auto id=[&](ExprId value){return reverse?tree.expressions.size()-1-value.value:value.value;};
  test::Check(bool(plan.replacements[id(store)])==!form);
  for(const auto sequence:sequences)test::Check(bool(plan.replacements[id(sequence)])==!form);
  std::ostringstream out;const auto printed=PrintFunctionTree(tree,out);
  test::Check(std::holds_alternative<PrintedFunction>(printed));
  for(const auto range:std::get<PrintedFunction>(printed).expressions)test::Check(range.begin<range.end && range.end<=out.str().size());
  if(emit)std::cout<<nlohmann::json{{"source",out.str()},{"operands",true},{"form",form},{"count",count}}<<'\n';
  return plan.work;
}

std::uint64_t InitializationReads(unsigned form,unsigned count,bool reverse,bool emit) {
  Fixture f;auto& tree=f.tree;
  const auto early=f.Stmt(ExpressionStmt{form?f.Store(2):f.Read(2)});
  std::vector<StmtId> body{f.Stmt(IfStmt{f.Read(1),f.Block({early},f.Scope()),{}})};
  for(unsigned i=0;i<count;++i)body.push_back(f.Stmt(ExpressionStmt{f.Call(0)}));
  body.push_back(f.Stmt(DeclarationStmt{{2},f.Call(1)}));
  const auto late=f.Stmt(ExpressionStmt{f.Read(2)});body.push_back(late);
  body.push_back(f.Stmt(ReturnStmt{f.Read(2)}));f.Finish(body,reverse);
  test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto result=PlanBindingInitializers(tree,loops,nullish);test::Check(std::holds_alternative<BindingInitializers>(result));
  const auto& plan=std::get<BindingInitializers>(result);
  // TDZ before the declaration does not make every later read effectful.
  // An early write must retain RHS evaluation before the failing PutValue.
  test::Check(!plan.omitted[early.value]);test::Check(plan.omitted[late.value]);
  std::ostringstream out;test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,out)));
  if(emit)std::cout<<nlohmann::json{{"source",out.str()},{"initialization",true},{"form",form},{"count",count}}<<'\n';
  return plan.work;
}
std::uint64_t CompoundCopies(unsigned form,unsigned count,bool reverse,bool emit) {
  Fixture f;auto& tree=f.tree;
  std::vector<StmtId> body{f.Stmt(DeclarationStmt{{2},{}})};
  unsigned source=1;
  if(form==7) {
    tree.names.push_back(u"origin");
    tree.symbols.push_back({{1},{4},BindingKind::kLocal,DeclarationKind::kConst,NameEvidence::kMetadata,{}});
    body.push_back(f.Stmt(DeclarationStmt{{3},f.Read(1)}));source=3;
  }
  const auto observe=[&](ExprId value) {
    const auto first=static_cast<std::uint32_t>(tree.arguments.size());tree.arguments.push_back(value);
    return f.Stmt(ExpressionStmt{f.Expr(CallExpr{f.Read(0),{first,1},CallReference::kValue})});
  };
  if(form==4)body.push_back(observe(f.Read(2)));
  auto rhs=f.Expr(SequenceExpr{f.Call(1),f.Read(source)});
  if(form==1||form==5)rhs=f.Expr(ConditionalExpr{f.Read(1),rhs,form==5?f.Call(2):f.Expr(SequenceExpr{f.Call(2),f.Read(1)})});
  if(form==2)rhs=f.Expr(LogicalExpr{TreeLogical::kOr,f.Read(1),rhs});
  for(unsigned i=0;i<count;++i)rhs=f.Expr(SequenceExpr{f.Call(0),rhs});
  const auto store=f.Stmt(ExpressionStmt{f.Expr(AssignmentExpr{SymbolTarget{{2}},rhs})});
  if(form==6) {
    const auto scope=f.Scope(ScopeKind::kCatch);
    tree.symbols.push_back({scope,{3},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
    body.push_back(f.Stmt(TryStmt{f.Block({store},f.Scope()),CatchClause{{3},f.Block({f.Stmt(ReturnStmt{f.Read(2)})},scope)},{}}));
  } else body.push_back(store);
  if(form==3)body.push_back(f.Stmt(ExpressionStmt{f.Expr(AssignmentExpr{SymbolTarget{{1}},f.Call(3)})}));
  body.push_back(observe(f.Read(2)));body.push_back(f.Stmt(ReturnStmt{f.Read(2)}));f.Finish(body,reverse);
  test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto planned=PlanBindingInitializers(tree,loops,nullish);test::Check(std::holds_alternative<BindingInitializers>(planned));
  const auto& plan=std::get<BindingInitializers>(planned);
  const bool expected=form<3 || form==7;
  if(plan.aliases[2].has_value()!=expected)std::cerr<<"compound copy form="<<form<<" count="<<count<<" reverse="<<reverse<<'\n';
  test::Check(plan.aliases[2].has_value()==expected);
  if(expected)test::Check(plan.aliases[2]==SymbolId{source});
  if(!count) {
    test::Check(std::holds_alternative<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes,plan.work})));
    test::Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes-1,plan.work}))==TreeStructureError::kResourceLimit);
    test::Check(std::get<TreeStructureError>(PlanBindingInitializers(tree,loops,nullish,{plan.allocated_bytes,plan.work-1}))==TreeStructureError::kWorkLimit);
  }
  std::ostringstream out;test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,out)));
  if(emit)std::cout<<nlohmann::json{{"source",out.str()},{"copy",true},{"form",form},{"count",count}}<<'\n';
  return plan.work;
}

std::uint64_t InertStatements(unsigned form,unsigned count,bool reverse,bool wrapped,bool emit) {
  Fixture f;auto& tree=f.tree;
  // No compiler temporary participates: omission cannot depend on another
  // pass first replacing a store, nor on private-storage demand cleanup.
  tree.symbols[2].kind=BindingKind::kLocal;tree.symbols[2].temporary.reset();
  std::vector<StmtId> body,observations;
  const auto declaration=f.Stmt(DeclarationStmt{{2},form==7?std::nullopt:std::optional{f.Read(1)}});
  if(form!=2)body.push_back(declaration);
  for(unsigned i=0;i<=count;++i) {
    auto value=form==4?f.Call(1):form==5?f.Expr(TreeLiteral{IntegerLiteral{0}}):
        f.Read(form==1||form==2||form==7?2:1);
    if(form==3)value=f.Expr(UnaryExpr{TreeUnary::kPlus,value});
    if(form==6)value=f.Expr(ConditionalExpr{value,f.Read(0),f.Read(1)});
    // The equivalent comma forces an upstream replacement; direct and wrapped
    // expressions must receive the same effect-based omission decision.
    if(wrapped)value=f.Expr(SequenceExpr{f.Expr(TreeLiteral{IntegerLiteral{0}}),value});
    const auto statement=f.Stmt(ExpressionStmt{value});
    body.push_back(statement);observations.push_back(statement);
  }
  if(form==2)body.push_back(declaration);
  body.push_back(f.Stmt(ReturnStmt{f.Read(1)}));f.Finish(body,reverse);
  test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));
  const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto planned=PlanBindingInitializers(tree,loops,nullish);
  test::Check(std::holds_alternative<BindingInitializers>(planned));
  const auto& plan=std::get<BindingInitializers>(planned);
  for(const auto statement:observations)test::Check(bool(plan.omitted[statement.value])==(form!=2&&form!=3&&form!=4));
  test::Check(!plan.omitted[declaration.value]);
  std::ostringstream out;test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,out)));
  if(emit)std::cout<<nlohmann::json{{"source",out.str()},{"inert_statement",true},{"form",form},{"count",count}}<<'\n';
  return plan.work;
}

std::uint64_t DiscardedResults(unsigned form,unsigned count,bool reverse,bool emit) {
  Fixture f;auto& tree=f.tree;std::vector<ExprId> sequences;
  auto value=f.Call(1);
  for(unsigned i=0;i<=count;++i) {
    auto right=f.Read(2);if(form==2)right=f.Expr(UnaryExpr{TreeUnary::kPlus,right});
    value=f.Expr(SequenceExpr{value,right});sequences.push_back(value);
  }
  auto result=value;
  if(form==3)result=f.Expr(ConditionalExpr{f.Read(1),value,f.Call(2)});
  if(form==4)result=f.Expr(LogicalExpr{TreeLogical::kOr,value,f.Call(2)});
  if(form==5)result=f.Expr(LogicalExpr{TreeLogical::kOr,f.Read(1),value});
  std::vector<StmtId> body{f.Stmt(DeclarationStmt{{2},f.Read(1)}),
      f.Stmt(form==1?TreeStatement{ReturnStmt{result}}:TreeStatement{ExpressionStmt{result}})};
  if(form!=1)body.push_back(f.Stmt(ReturnStmt{f.Read(1)}));
  f.Finish(body,reverse);test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto plan=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  for(const auto sequence:sequences) {
    const auto id=reverse?tree.expressions.size()-1-sequence.value:sequence.value;
    const bool expected=form!=2 && (form==0||form==3||form==5||sequence!=sequences.back());
    if(plan.replacements[id].has_value()!=expected)std::cerr<<"discarded result form="<<form<<" count="<<count<<'\n';
    test::Check(plan.replacements[id].has_value()==expected);
  }
  std::ostringstream out;const auto printed=PrintFunctionTree(tree,out);
  test::Check(std::holds_alternative<PrintedFunction>(printed));
  for(const auto range:std::get<PrintedFunction>(printed).expressions)
    test::Check(range.begin<range.end && range.end<=out.str().size());
  if(emit)std::cout<<nlohmann::json{{"source",out.str()},{"discarded_result",true},{"form",form},{"count",count}}<<'\n';
  return plan.work;
}

void ScopedCopies(bool reverse,bool emit) {
  Fixture f;auto& tree=f.tree;const auto scope=f.Scope();
  tree.names.push_back(u"second");tree.names.push_back(u"selected");
  tree.symbols.push_back({{1},{4},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
  tree.symbols.push_back({scope,{5},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
  tree.symbols.push_back({scope,{0},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
  tree.symbols.push_back({scope,{1},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
  const auto store=[&](unsigned target,ExprId rhs){return f.Stmt(ExpressionStmt{f.Expr(AssignmentExpr{SymbolTarget{{target}},rhs})});};
  const auto choice=f.Expr(ConditionalExpr{f.Read(2),f.Read(3),f.Read(3)});
  const auto inner=f.Block({f.Stmt(DeclarationStmt{{4},{}}),store(4,choice),
      f.Stmt(DeclarationStmt{{5},f.Expr(TreeLiteral{IntegerLiteral{1}})}),
      f.Stmt(DeclarationStmt{{6},f.Expr(TreeLiteral{IntegerLiteral{2}})}),f.Stmt(ReturnStmt{f.Read(4)})},scope);
  f.Finish({f.Stmt(DeclarationStmt{{2},{}}),store(2,f.Read(0)),
      f.Stmt(DeclarationStmt{{3},{}}),store(3,f.Read(1)),inner},reverse);
  test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto plan=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  // Keeping selected also keeps BOTH upstream copies: their original parameter
  // names resolve to TDZ shadows at the conditional's retained occurrence.
  for(unsigned i:{2U,3U,4U})test::Check(!plan.aliases[i]);
  std::ostringstream out;test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,out)));
  if(emit)std::cout<<nlohmann::json{{"source",out.str()},{"scoped_copy",true}}<<'\n';
}

void BindingObligations() {
  // Cartesian semantic facts, independent of CFG reachability and arena order.
  // A reference in retained syntax requires a binding even if its value is dead.
  for(bool observable:{false,true})for(bool read:{false,true})for(bool write:{false,true})
    for(bool drop_region:{false,true})for(bool drop_write:{false,true})for(bool reverse:{false,true}) {
      RecoveryFlow flow{};flow.bindings={{BindingKind::kCompilerTemporary,
          observable?FlowBindingExposure::kObservable:FlowBindingExposure::kPrivate}};
      flow.statements.resize(2);flow.expressions.resize(1);flow.events.resize(3);
      flow.discarded.push_back({{1}});
      flow.definitions.push_back({{0},{0},FlowDefinitionKind::kUndefined,{},{},StmtId{0}});
      flow.events[0].definition=FlowDefinitionId{0};
      if(read){flow.events[1].read=SymbolId{0};flow.events[1].discarded=DiscardedId{0};}
      if(write) {
        flow.definitions.push_back({{2},{0},FlowDefinitionKind::kAssignment,{},{},{},ExprId{0}});
        flow.events[2].definition=FlowDefinitionId{1};flow.events[2].discarded=DiscardedId{0};
      }
      DefinitionElisions delta;if(drop_region)delta.statements.push_back({1});if(drop_write)delta.writes.push_back({0});
      if(reverse)std::reverse(flow.events.begin(),flow.events.end());
      const auto result=std::get<BindingElisions>(PlanBindingElisions(flow,delta));
      const bool expected=!observable && (drop_region || (!read && (!write||drop_write)));
      test::Check(result.declarations.size()==std::size_t(expected));
      test::Check(std::holds_alternative<BindingElisions>(PlanBindingElisions(flow,delta,{result.allocated_bytes,result.work})));
      test::Check(std::get<TreeStructureError>(PlanBindingElisions(flow,delta,{result.allocated_bytes-1,result.work}))==TreeStructureError::kResourceLimit);
      test::Check(std::get<TreeStructureError>(PlanBindingElisions(flow,delta,{result.allocated_bytes,result.work-1}))==TreeStructureError::kWorkLimit);
  }
}

}
int main(int argc,char**) {
  BindingObligations();
  for(unsigned form=0;form<8;++form)for(bool reverse:{false,true})for(bool wrapped:{false,true}) {
    std::uint64_t previous=0;
    for(unsigned count:{0U,1U,200U,201U,400U,800U}) {
      const auto work=InertStatements(form,count,reverse,wrapped,argc>1);
      if(count>=400)test::Check(work<previous*3);previous=work;
    }
  }
  for(unsigned form=0;form<6;++form)for(bool reverse:{false,true}) {
    std::uint64_t previous=0;
    for(unsigned count:{0U,1U,200U,201U,400U,800U}) {
      const auto work=DiscardedResults(form,count,reverse,argc>1);
      if(count>=400)test::Check(work<previous*3);previous=work;
    }
  }
  for(bool reverse:{false,true})ScopedCopies(reverse,argc>1);
  for(unsigned form=0;form<8;++form)for(bool reverse:{false,true}) {
    std::uint64_t previous=0;
    for(unsigned count:{0U,1U,200U,201U,400U,800U}) {
      const auto work=CompoundCopies(form,count,reverse,argc>1);
      if(count>=400)test::Check(work<previous*3);previous=work;
    }
  }
  for(unsigned form=0;form<2;++form)for(bool reverse:{false,true}) {
    std::uint64_t previous=0;
    for(unsigned count:{0U,200U,201U,400U,800U}) {
      const auto work=DiscardedOperands(form,count,reverse,argc>1);
      if(count>=400)test::Check(work<previous*3);previous=work;
    }
  }
  for(unsigned form=0;form<2;++form)for(bool reverse:{false,true}) {
    std::uint64_t previous=0;
    for(unsigned count:{0U,200U,201U,400U,800U}) {
      const auto work=InitializationReads(form,count,reverse,argc>1);
      if(count>=400)test::Check(work<previous*3);previous=work;
    }
  }
  for(unsigned form=0;form<6;++form)for(bool reverse:{false,true}) {
    std::uint64_t previous=0;
    for(unsigned count:{0U,200U,201U,400U,800U}) {
      const auto work=EmptyBinding(form,count,reverse,argc>1);
      if(count>=400)test::Check(work<previous*3);previous=work;
    }
  }
  for(bool cycle:{false,true})for(bool observed:{false,true})for(bool reverse:{false,true}) {
    std::uint64_t previous=0;
    for(unsigned size:{2U,200U,201U,400U,800U}) {
      const auto work=Chain(size,cycle,observed,reverse,argc>1);
      if(size>=400)test::Check(work<previous*3);
      previous=work;
    }
  }
  for(unsigned form=0;form<7;++form)for(bool reverse:{false,true}) {
    std::uint64_t previous=0;
    for(unsigned count:{0U,1U,200U,201U,400U,800U}) {
      const auto work=Check(form,count,reverse,argc>1);
      if(count>=400)test::Check(work<previous*3);
      previous=work;
      if(count==0)Check(form,count,reverse,false,true);
    }
  }
}
