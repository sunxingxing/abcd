#include "entry_initializer_checks.h"
#include "../../../src/source/binding_initializer.h"
#include "../../../src/source/loop_syntax.h"
#include "../../../src/source/nullish_syntax.h"
#include "../../../src/source/family_print.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
#include <nlohmann/json.hpp>
#include <sstream>
using namespace abcd;
using namespace abcd::source;
namespace {
struct EntryFixture {
  FunctionTree tree{};
  EntryFixture() {
    tree.callable=CallableKind::kFunction;tree.strictness=Strictness::kStrict;
    tree.parameter_scope={0};tree.body_scope={1};tree.body={0};
    tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
    tree.names={u"input",u"report",u"other",u"flag"};tree.origins={OriginalSite{{0},{0}}};
    for(unsigned i=0;i<4;++i) {
      tree.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}});
      tree.parameters.push_back({{i},{},false});
    }
    tree.statements.push_back({BlockStmt{{1},0,0},{0}});
  }
  ExprId Expr(TreeExpression value) {const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};tree.expressions.push_back({std::move(value),{0}});return id;}
  StmtId Stmt(TreeStatement value) {const StmtId id{static_cast<std::uint32_t>(tree.statements.size())};tree.statements.push_back({std::move(value),{0}});return id;}
  ExprId Read(SymbolId symbol) {return Expr(SymbolExpr{symbol});}
  StmtId Store(SymbolId target,SymbolId source) {return Stmt(ExpressionStmt{Expr(AssignmentExpr{SymbolTarget{target},Read(source)})});}
  StmtId Report(SymbolId symbol) {const auto first=static_cast<std::uint32_t>(tree.arguments.size());tree.arguments.push_back(Read(symbol));return Stmt(ExpressionStmt{Expr(CallExpr{Read({1}),{first,1},CallReference::kValue})});}
  StmtId Block(const std::vector<StmtId>& children,ScopeKind kind=ScopeKind::kBlock) {
    const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({kind,ScopeId{1}});
    const auto first=static_cast<std::uint32_t>(tree.children.size());tree.children.insert(tree.children.end(),children.begin(),children.end());
    return Stmt(BlockStmt{scope,first,static_cast<std::uint32_t>(children.size())});
  }
  void Finish(const std::vector<StmtId>& children) {
    const auto first=static_cast<std::uint32_t>(tree.children.size());tree.children.insert(tree.children.end(),children.begin(),children.end());
    std::get<BlockStmt>(tree.statements[tree.body.value].statement)={{1},first,static_cast<std::uint32_t>(children.size())};
    for(unsigned i=0;i<tree.statements.size();++i)if(const auto* block=std::get_if<BlockStmt>(&tree.statements[i].statement))for(unsigned j=0;j<=block->child_count;++j) {
      const auto anchor=static_cast<std::uint32_t>(tree.anchors.size());tree.anchors.push_back({BlockBoundary{{i},j},{0}});tree.operations.push_back({{anchor},AnchorId{anchor}});
    }
  }
  void Reverse() {
    const auto expression=[&](ExprId id){return ExprId{static_cast<std::uint32_t>(tree.expressions.size()-1-id.value)};};
    const auto statement=[&](StmtId id){return StmtId{static_cast<std::uint32_t>(tree.statements.size()-1-id.value)};};
    const auto symbol=[&](SymbolId id){return SymbolId{static_cast<std::uint32_t>(tree.symbols.size()-1-id.value)};};
    const auto scope=[&](ScopeId id){return ScopeId{static_cast<std::uint32_t>(tree.scopes.size()-1-id.value)};};
    for(auto& parameter:tree.parameters){parameter.symbol=symbol(parameter.symbol);if(parameter.initializer)parameter.initializer=expression(*parameter.initializer);}
    for(auto& node:tree.expressions)std::visit([&](auto& value){using T=std::decay_t<decltype(value)>;
      if constexpr(std::is_same_v<T,SymbolExpr>)value.symbol=symbol(value.symbol);
      else if constexpr(std::is_same_v<T,AssignmentExpr>){std::get<SymbolTarget>(value.target).symbol=symbol(std::get<SymbolTarget>(value.target).symbol);value.value=expression(value.value);}
      else if constexpr(std::is_same_v<T,CallExpr>)value.callee=expression(value.callee);
      else if constexpr(std::is_same_v<T,AwaitExpr>||std::is_same_v<T,YieldExpr>||std::is_same_v<T,UnaryExpr>)value.operand=expression(value.operand);
      else test::Check(std::holds_alternative<TreeLiteral>(node.expression)||std::holds_alternative<FunctionExpr>(node.expression)||std::holds_alternative<EmptyObjectExpr>(node.expression));
    },node.expression);
    for(auto& argument:tree.arguments)argument=expression(argument);
    for(auto& node:tree.statements)std::visit([&](auto& value){using T=std::decay_t<decltype(value)>;
      if constexpr(std::is_same_v<T,BlockStmt>)value.scope=scope(value.scope);
      else if constexpr(std::is_same_v<T,DeclarationStmt>){value.symbol=symbol(value.symbol);if(value.initializer)value.initializer=expression(*value.initializer);}
      else if constexpr(std::is_same_v<T,ExpressionStmt>)value.expression=expression(value.expression);
      else if constexpr(std::is_same_v<T,ReturnStmt>){if(value.value)value.value=expression(*value.value);}
      else if constexpr(std::is_same_v<T,ThrowStmt>)value.value=expression(value.value);
      else if constexpr(std::is_same_v<T,IfStmt>){value.condition=expression(value.condition);value.consequent=statement(value.consequent);}
      else if constexpr(std::is_same_v<T,LoopStmt>)value.body=statement(value.body);
      else if constexpr(std::is_same_v<T,BreakStmt>)value.target=statement(value.target);
      else if constexpr(std::is_same_v<T,TryStmt>){value.body=statement(value.body);if(value.handler){value.handler->body=statement(value.handler->body);value.handler->parameter=symbol(value.handler->parameter);}if(value.finalizer)value.finalizer=statement(*value.finalizer);}
      else test::Check(false);
    },node.statement);
    for(auto& child:tree.children)child=statement(child);for(auto& captured:tree.closure_captures)captured=symbol(captured);
    for(auto& anchor:tree.anchors)std::get<BlockBoundary>(anchor.position).block=statement(std::get<BlockBoundary>(anchor.position).block);
    for(auto& binding:tree.symbols)binding.scope=scope(binding.scope);for(auto& lexical:tree.scopes)if(lexical.parent)lexical.parent=scope(*lexical.parent);
    tree.parameter_scope=scope(tree.parameter_scope);tree.body_scope=scope(tree.body_scope);tree.body=statement(tree.body);if(tree.self)tree.self=symbol(*tree.self);
    std::reverse(tree.expressions.begin(),tree.expressions.end());std::reverse(tree.statements.begin(),tree.statements.end());std::reverse(tree.symbols.begin(),tree.symbols.end());std::reverse(tree.scopes.begin(),tree.scopes.end());
  }
};
std::uint64_t Entry(unsigned form,unsigned count,bool reverse,bool emit) {
  EntryFixture fixture;auto& tree=fixture.tree;const bool caught=form==2||form==3||form==7||form==11;
  const bool sloppy=form==5||form==8||form==9||form==17;tree.strictness=sloppy?Strictness::kSloppy:Strictness::kStrict;
  if(form==8)tree.parameters[0].initializer=fixture.Expr(TreeLiteral{IntegerLiteral{42}});
  if(form==9)tree.callable=CallableKind::kArrow;
  if(form==17)tree.parameters.back().rest=true;
  if(form==13||form==26)tree.callable=CallableKind::kAsyncFunction;if(form==14||form==27)tree.callable=CallableKind::kGenerator;
  SymbolId source{0};std::optional<StmtId> protected_body,handler;
  if(caught) {
    protected_body=fixture.Block({fixture.Stmt(ThrowStmt{fixture.Read({0})})});handler=fixture.Block({},ScopeKind::kCatch);
    source={static_cast<std::uint32_t>(tree.symbols.size())};tree.names.push_back(u"caught");tree.symbols.push_back({std::get<BlockStmt>(tree.statements[handler->value].statement).scope,{static_cast<std::uint32_t>(tree.names.size()-1)},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kMetadata,{}});
  }
  if(form==4) {
    const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kFunctionSelf,{}});tree.scopes[0].parent=scope;
    const NameId name{static_cast<std::uint32_t>(tree.names.size())};tree.names.push_back(u"owner");source={static_cast<std::uint32_t>(tree.symbols.size())};tree.self=source;tree.inferred_name=name;
    tree.symbols.push_back({scope,name,BindingKind::kFunctionSelf,DeclarationKind::kConst,NameEvidence::kMetadata,{}});
  }
  std::vector<StmtId> cells;
  std::optional<SymbolId> replacement;
  if(form==16) {
    // The current canonical capture domain represents mutable lexical cells,
    // including promoted source formals, as Local bindings rather than a raw
    // invocation slot. Keep that distinction in this valid family fixture.
    for(unsigned i=0;i<2;++i) {
      const SymbolId symbol{static_cast<std::uint32_t>(tree.symbols.size())};const NameId name{static_cast<std::uint32_t>(tree.names.size())};tree.names.push_back(i?u"replacement":u"state");
      tree.symbols.push_back({{1},name,BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
      cells.push_back(fixture.Stmt(DeclarationStmt{symbol,fixture.Read(i?SymbolId{2}:SymbolId{0})}));if(i)replacement=symbol;else source=symbol;
    }
  }
  if(form>=18) {
    source={static_cast<std::uint32_t>(tree.symbols.size())};const NameId name{static_cast<std::uint32_t>(tree.names.size())};tree.names.push_back(u"state");
    tree.symbols.push_back({{1},name,BindingKind::kLocal,form==19?DeclarationKind::kConst:DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    std::optional<ExprId> value;
    if(form!=23&&form<30) {
      if(form==22||form==25||form==26||form==27) {value=fixture.Expr(CallExpr{fixture.Read({2}),{0,0},CallReference::kValue});if(form==26)value=fixture.Expr(AwaitExpr{*value});if(form==27)value=fixture.Expr(YieldExpr{*value});}
      else value=form==28?fixture.Read(source):form==29?fixture.Expr(EmptyObjectExpr{}):fixture.Read({0});
    }
    cells.push_back(fixture.Stmt(DeclarationStmt{source,value}));
  }
  std::vector<StmtId> declarations,copies;
  auto last=source;
  for(unsigned i=0;i<count;++i) {
    const SymbolId symbol{static_cast<std::uint32_t>(tree.symbols.size())};const NameId name{static_cast<std::uint32_t>(tree.names.size())};const auto digits=std::to_string(i);tree.names.push_back(u"copy"+std::u16string(digits.begin(),digits.end()));
    tree.symbols.push_back({{1},name,BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
    declarations.push_back(fixture.Stmt(DeclarationStmt{symbol,{}}));copies.push_back(fixture.Store(symbol,last));last=symbol;
  }
  std::vector<StmtId> body=copies;
  if(form>=30) {
    const auto value=form==33?fixture.Expr(CallExpr{fixture.Read({2}),{0,0},CallReference::kValue}):fixture.Read({0});
    const auto store=fixture.Stmt(ExpressionStmt{fixture.Expr(AssignmentExpr{SymbolTarget{source},value})});
    if(form==32)body.insert(body.begin(),fixture.Stmt(IfStmt{fixture.Read({3}),fixture.Block({store}),{}}));
    else body.insert(body.begin(),store);
  }
  if(form==11) {
    const auto branch=fixture.Block(copies);tree.scopes[std::get<BlockStmt>(tree.statements[branch.value].statement).scope.value].parent=std::get<BlockStmt>(tree.statements[handler->value].statement).scope;
    body={fixture.Stmt(IfStmt{fixture.Read({3}),branch,{}})};
  }
  if(form==13||form==14) {const auto call=fixture.Expr(CallExpr{fixture.Read({2}),{0,0},CallReference::kValue});body.push_back(fixture.Stmt(ExpressionStmt{fixture.Expr(form==13?TreeExpression{AwaitExpr{call}}:TreeExpression{YieldExpr{call}})}));}
  std::optional<FunctionTree> child;
  if(form==16) {
    tree.closure_captures={source,*replacement};FunctionTree nested{};nested.owner={1};nested.callable=CallableKind::kArrow;nested.strictness=Strictness::kStrict;nested.parameter_scope={0};nested.body_scope={1};nested.capture_scope=ScopeId{2};nested.body={0};
    nested.scopes={{ScopeKind::kParameters,ScopeId{2}},{ScopeKind::kFunctionBody,ScopeId{0}},{ScopeKind::kCaptures,{}}};nested.names={u"state",u"replacement"};nested.origins={OriginalSite{{0},{0}}};
    for(unsigned i=0;i<2;++i){nested.symbols.push_back({{2},{i},BindingKind::kCapture,DeclarationKind::kLet,NameEvidence::kMetadata,{}});nested.captures.push_back({{i},{tree.owner,i?*replacement:source}});}
    nested.expressions={{SymbolExpr{{1}},{0}},{AssignmentExpr{SymbolTarget{{0}},ExprId{0}},{0}}};nested.children={StmtId{1}};nested.statements={{BlockStmt{{1},0,1},{0}},{ExpressionStmt{ExprId{1}},{0}}};nested.anchors={{BlockBoundary{{0},0},{0}},{BlockBoundary{{0},1},{0}}};nested.operations={{{0},AnchorId{0}},{{1},AnchorId{1}}};
    child=std::move(nested);body.push_back(fixture.Stmt(ExpressionStmt{fixture.Expr(CallExpr{fixture.Expr(FunctionExpr{{1},{0,2}}),{0,0},CallReference::kValue})}));
  }
  for(unsigned i=0;i<count;++i)body.push_back(fixture.Report(last));
  if(form==1||form==3||form==20||form==31)body.push_back(fixture.Store(source,{2}));
  if(form==15)body.push_back(fixture.Stmt(ReturnStmt{fixture.Expr(CallExpr{fixture.Read(last),{0,0},CallReference::kValue})}));
  else body.push_back(fixture.Stmt(ReturnStmt{(form==25||form==33)?std::nullopt:std::optional{fixture.Read(last)}}));
  if(form==10) {
    const auto first=copies.size();std::vector<StmtId> tail(body.begin()+first,body.end());body.resize(first);const auto block=fixture.Block(tail);
    const auto scope=std::get<BlockStmt>(tree.statements[block.value].statement).scope;
    const SymbolId shadow{static_cast<std::uint32_t>(tree.symbols.size())};tree.symbols.push_back({scope,{0},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    const auto declared=fixture.Stmt(DeclarationStmt{shadow,fixture.Expr(TreeLiteral{IntegerLiteral{123}})});auto& sequence=std::get<BlockStmt>(tree.statements[block.value].statement);tree.children.insert(tree.children.begin()+sequence.first_child,declared);++sequence.child_count;body.push_back(block);
  }
  if(form==21)body.insert(body.begin()+copies.size(),cells.front());
  if(form==24) {
    std::vector<StmtId> inner=cells;inner.insert(inner.end(),copies.begin(),copies.end());const auto block=fixture.Block(inner);
    tree.symbols[source.value].scope=std::get<BlockStmt>(tree.statements[block.value].statement).scope;
    body.erase(body.begin(),body.begin()+copies.size());body.insert(body.begin(),block);
  }
  if(form==25||form==33) {
    const auto returned=body.back();body.pop_back();std::vector<StmtId> inner=form==25?cells:std::vector<StmtId>{};inner.insert(inner.end(),body.begin(),body.end());const auto block=fixture.Block(inner);
    const auto scope=std::get<BlockStmt>(tree.statements[block.value].statement).scope;if(form==25)tree.symbols[source.value].scope=scope;
    const auto loop=fixture.Stmt(LoopStmt{block,{}});const auto exit=fixture.Block({fixture.Stmt(BreakStmt{loop})});tree.scopes[std::get<BlockStmt>(tree.statements[exit.value].statement).scope.value].parent=scope;
    const auto test=fixture.Expr(CallExpr{fixture.Read({3}),{0,0},CallReference::kValue});const auto guard=fixture.Stmt(IfStmt{fixture.Expr(UnaryExpr{TreeUnary::kNot,test}),exit,{}});
    auto& sequence=std::get<BlockStmt>(tree.statements[block.value].statement);tree.children.insert(tree.children.begin()+sequence.first_child,guard);++sequence.child_count;
    ++std::get<BlockStmt>(tree.statements[exit.value].statement).first_child;body={loop,returned};
  }
  if(form==21||form==24||form==25)cells.clear();
  if(caught) {
    const auto returned=body.back();if(form==7)body.pop_back();const auto first=static_cast<std::uint32_t>(tree.children.size());tree.children.insert(tree.children.end(),body.begin(),body.end());auto& block=std::get<BlockStmt>(tree.statements[handler->value].statement);block.first_child=first;block.child_count=body.size();
    const auto finalizer=form==7?std::optional{fixture.Block({fixture.Report(last)})}:std::nullopt;
    body={fixture.Stmt(TryStmt{*protected_body,CatchClause{source,*handler},finalizer})};if(form==7)body.push_back(returned);
  }
  declarations.insert(declarations.begin(),cells.begin(),cells.end());declarations.insert(declarations.end(),body.begin(),body.end());fixture.Finish(declarations);
  if(reverse) {fixture.Reverse();if(child)for(auto& capture:child->captures)capture.target.symbol={static_cast<std::uint32_t>(tree.symbols.size()-1-capture.target.symbol.value)};}
  const auto structure=VerifyTreeStructure(tree);
  if(const auto* error=std::get_if<TreeStructureError>(&structure))std::cerr<<"entry="<<form<<", count="<<count<<", reverse="<<reverse<<", error="<<static_cast<unsigned>(*error)<<'\n';
  test::Check(std::holds_alternative<TreeStructure>(structure));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));const auto plan=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  std::ostringstream output;PrintedFunction ranges;
  if(child) {ConstructedFamily family;family.root={0};family.members={{tree,{},{}},{std::move(*child),{},{}}};const auto printed=PrintFamilyDraft(family,output);test::Check(std::holds_alternative<PrintedFamilyDraft>(printed));ranges=std::get<PrintedFamilyDraft>(printed).members[0];}
  else {const auto printed=PrintFunctionTree(tree,output);test::Check(std::holds_alternative<PrintedFunction>(printed));ranges=std::get<PrintedFunction>(printed);}
  const bool folded=count&&form!=1&&form!=3&&form!=5&&form!=7&&form!=10&&form!=11&&form!=16&&form!=20&&form!=21&&form!=24&&form!=28&&form!=31&&form!=32;
  test::Check(plan.aliases[reverse?tree.symbols.size()-1-last.value:last.value].has_value()==folded);
  for(const auto range:ranges.expressions)test::Check(range.begin<=range.end&&range.end<=output.str().size());for(const auto anchor:ranges.anchors)test::Check(anchor>0&&anchor<=output.str().size());
  if(emit)std::cout<<nlohmann::json{{"source",output.str()},{"entry",form},{"count",count},{"folded",folded},{"strict",!sloppy}}<<'\n';
  return plan.work;
}
}
void CheckEntryInitializers(bool emit) {
  for(unsigned form:{0U,1U,2U,3U,4U,5U,6U,7U,8U,9U,10U,11U,13U,14U,15U,16U,17U,18U,19U,20U,21U,22U,23U,24U,25U,26U,27U,28U,29U,30U,31U,32U,33U}) {
    std::uint64_t small=0,large=0;
    for(unsigned count:{0U,1U,200U,201U,400U,800U})for(bool reverse:{false,true}) {
      if(!count&&(form==7||form==10||form==11||form==21||form==24))continue;
      const auto work=Entry(form,count,reverse,emit);if(count==400&&!reverse)small=work;if(count==800&&!reverse)large=work;
    }
    test::Check(large<small*3);
  }
}
