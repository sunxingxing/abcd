#include "expression_initializer_checks.h"
#include "../../../src/source/binding_initializer.h"
#include "../../../src/source/loop_syntax.h"
#include "../../../src/source/nullish_syntax.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
#include <nlohmann/json.hpp>
#include <sstream>
using namespace abcd;
using namespace abcd::source;
namespace {
struct Expressions {
  FunctionTree tree{};
  ExprId Expr(TreeExpression value) {const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};tree.expressions.push_back({std::move(value),{0}});return id;}
  StmtId Stmt(TreeStatement value) {const StmtId id{static_cast<std::uint32_t>(tree.statements.size())};tree.statements.push_back({std::move(value),{0}});return id;}
  ExprId Read(SymbolId id) {return Expr(SymbolExpr{id});}
  ExprId Call(SymbolId symbol,std::vector<ExprId> arguments) {
    const auto first=static_cast<std::uint32_t>(tree.arguments.size());tree.arguments.insert(tree.arguments.end(),arguments.begin(),arguments.end());
    return Expr(CallExpr{Read(symbol),{first,static_cast<std::uint32_t>(arguments.size())},CallReference::kValue});
  }
  void Reverse() {
    const auto expression=[&](ExprId id){return ExprId{static_cast<std::uint32_t>(tree.expressions.size()-1-id.value)};};
    const auto statement=[&](StmtId id){return StmtId{static_cast<std::uint32_t>(tree.statements.size()-1-id.value)};};
    const auto symbol=[&](SymbolId id){return SymbolId{static_cast<std::uint32_t>(tree.symbols.size()-1-id.value)};};
    const auto scope=[&](ScopeId id){return ScopeId{static_cast<std::uint32_t>(tree.scopes.size()-1-id.value)};};
    for(auto& node:tree.expressions)std::visit([&](auto& value){using T=std::decay_t<decltype(value)>;
      if constexpr(std::is_same_v<T,SymbolExpr>)value.symbol=symbol(value.symbol);
      else if constexpr(std::is_same_v<T,AssignmentExpr>){std::get<SymbolTarget>(value.target).symbol=symbol(std::get<SymbolTarget>(value.target).symbol);value.value=expression(value.value);}
      else if constexpr(std::is_same_v<T,CallExpr>)value.callee=expression(value.callee);
      else if constexpr(std::is_same_v<T,SequenceExpr>||std::is_same_v<T,LogicalExpr>){value.left=expression(value.left);value.right=expression(value.right);}
      else if constexpr(std::is_same_v<T,ConditionalExpr>){value.condition=expression(value.condition);value.consequent=expression(value.consequent);value.alternate=expression(value.alternate);}
      else if constexpr(std::is_same_v<T,AwaitExpr>||std::is_same_v<T,YieldExpr>||std::is_same_v<T,UnaryExpr>)value.operand=expression(value.operand);
      else test::Check(std::holds_alternative<TreeLiteral>(node.expression)||std::holds_alternative<EmptyObjectExpr>(node.expression));
    },node.expression);
    for(auto& node:tree.statements)std::visit([&](auto& value){using T=std::decay_t<decltype(value)>;
      if constexpr(std::is_same_v<T,BlockStmt>)value.scope=scope(value.scope);
      else if constexpr(std::is_same_v<T,DeclarationStmt>){value.symbol=symbol(value.symbol);if(value.initializer)value.initializer=expression(*value.initializer);}
      else if constexpr(std::is_same_v<T,ExpressionStmt>)value.expression=expression(value.expression);
      else if constexpr(std::is_same_v<T,ReturnStmt>){if(value.value)value.value=expression(*value.value);}
      else if constexpr(std::is_same_v<T,IfStmt>){value.condition=expression(value.condition);value.consequent=statement(value.consequent);}
      else if constexpr(std::is_same_v<T,LoopStmt>)value.body=statement(value.body);
      else if constexpr(std::is_same_v<T,BreakStmt>)value.target=statement(value.target);
      else test::Check(false);
    },node.statement);
    for(auto& argument:tree.arguments)argument=expression(argument);for(auto& child:tree.children)child=statement(child);
    for(auto& parameter:tree.parameters)parameter.symbol=symbol(parameter.symbol);
    for(auto& binding:tree.symbols)binding.scope=scope(binding.scope);
    for(auto& owned:tree.scopes)if(owned.parent)owned.parent=scope(*owned.parent);
    for(auto& anchor:tree.anchors)std::get<BlockBoundary>(anchor.position).block=statement(std::get<BlockBoundary>(anchor.position).block);
    tree.body=statement(tree.body);tree.body_scope=scope(tree.body_scope);tree.parameter_scope=scope(tree.parameter_scope);
    std::reverse(tree.expressions.begin(),tree.expressions.end());std::reverse(tree.statements.begin(),tree.statements.end());
    std::reverse(tree.symbols.begin(),tree.symbols.end());std::reverse(tree.scopes.begin(),tree.scopes.end());
  }
};
std::uint64_t Check(unsigned form,unsigned count,bool reverse,bool emit) {
  Expressions f;auto& tree=f.tree;tree.callable=form==11?CallableKind::kAsyncFunction:form==12?CallableKind::kGenerator:CallableKind::kFunction;
  tree.strictness=Strictness::kStrict;tree.parameter_scope={0};tree.body_scope={1};tree.body={0};
  tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};tree.names={u"input",u"report",u"other",u"flag"};tree.origins={OriginalSite{{0},{0}}};
  for(unsigned i=0;i<4;++i){tree.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}});tree.parameters.push_back({{i},{},false});}
  tree.statements.push_back({BlockStmt{{1},0,0},{0}});std::vector<StmtId> body;SymbolId root{form==17?2U:0U};
  if(form>=10&&form<=13) {
    root={4};tree.names.push_back(u"state");tree.symbols.push_back({{1},{4},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    auto initial=form==13?f.Expr(EmptyObjectExpr{}):f.Call({2},{});if(form==11)initial=f.Expr(AwaitExpr{initial});if(form==12)initial=f.Expr(YieldExpr{initial});
    body.push_back(f.Stmt(DeclarationStmt{root,initial}));
  }
  std::vector<SymbolId> copies;for(unsigned i=0;i<count;++i) {
    const SymbolId id{static_cast<std::uint32_t>(tree.symbols.size())};const NameId name{static_cast<std::uint32_t>(tree.names.size())};
    const auto text="copy"+std::to_string(i);tree.names.emplace_back(text.begin(),text.end());tree.symbols.push_back({{1},name,BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
    copies.push_back(id);body.push_back(f.Stmt(DeclarationStmt{id,{}}));
  }
  const auto last=copies.empty()?root:copies.back();std::optional<ExprId> chain;
  for(unsigned i=0;i<count;++i) {
    const auto source=form==8&&!i?copies[i]:i?copies[i-1]:root;
    const auto rhs=form==16&&!i?f.Expr(TreeLiteral{IntegerLiteral{0}}):f.Read(source);
    const auto store=f.Expr(AssignmentExpr{SymbolTarget{copies[i]},rhs});
    if(form==18&&!i)body.push_back(f.Stmt(ExpressionStmt{store}));else chain=chain?f.Expr(SequenceExpr{*chain,store}):store;
  }
  auto nested=chain?f.Expr(SequenceExpr{*chain,f.Read(last)}):f.Read(last);
  ExprId effect=nested;
  if(form==1)effect=f.Call({1},{f.Read(last),nested});
  else if(form==2)effect=f.Call({1},{nested,f.Read(last)});
  else if(form==3)effect=f.Expr(LogicalExpr{TreeLogical::kAnd,f.Read({3}),nested});
  else if(form==4)effect=f.Expr(LogicalExpr{TreeLogical::kAnd,f.Read({3}),f.Call({1},{nested,f.Read(last)})});
  else if(form==21||form==22)effect=f.Expr(LogicalExpr{form==21?TreeLogical::kOr:TreeLogical::kNullish,f.Read({3}),f.Call({1},{nested,f.Read(last)})});
  else if(form==5)effect=f.Expr(ConditionalExpr{f.Read({3}),nested,f.Expr(TreeLiteral{UndefinedLiteral{}})});
  else if(form==6)effect=f.Expr(ConditionalExpr{f.Read({3}),f.Call({1},{nested,f.Read(last)}),f.Expr(TreeLiteral{UndefinedLiteral{}})});
  else if(form==7)effect=f.Call({1},{f.Expr(SequenceExpr{f.Read(last),nested})});
  else if(form==17){const auto call=f.Expr(CallExpr{nested,{0,0},CallReference::kValue});effect=f.Call({1},{call});}
  else if(form!=15)effect=f.Call({1},{nested});
  if(form==18) {
    tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});const SymbolId shadow{static_cast<std::uint32_t>(tree.symbols.size())};
    tree.symbols.push_back({{2},{0},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    tree.children={f.Stmt(DeclarationStmt{shadow,f.Expr(TreeLiteral{IntegerLiteral{123}})}),f.Stmt(ExpressionStmt{effect}),f.Stmt(ExpressionStmt{f.Call({1},{f.Read(last)})}),f.Stmt(ReturnStmt{f.Read(last)})};
    body.push_back(f.Stmt(BlockStmt{{2},0,4}));
  } else if(form==15)body.push_back(f.Stmt(ReturnStmt{effect}));
  else {
    const auto statement=f.Stmt(ExpressionStmt{effect});
    if(form==14||form==19){tree.scopes.push_back({ScopeKind::kBlock,ScopeId{1}});tree.children.push_back(statement);const auto arm=f.Stmt(BlockStmt{{2},0,1});body.push_back(form==14?f.Stmt(IfStmt{f.Read({3}),arm,{}}):arm);}
    else body.push_back(statement);
    if(form==9)body.push_back(f.Stmt(ExpressionStmt{f.Expr(AssignmentExpr{SymbolTarget{last},f.Read({2})})}));
    if(form!=4&&form!=6&&form!=17&&form!=21&&form!=22)body.push_back(f.Stmt(ExpressionStmt{f.Call({1},{f.Read(last)})}));
    body.push_back(f.Stmt(ReturnStmt{form==4||form==6||form==17||form==21||form==22?std::optional<ExprId>{}:f.Read(last)}));
  }
  auto& block=std::get<BlockStmt>(tree.statements[0].statement);block.first_child=tree.children.size();block.child_count=body.size();tree.children.insert(tree.children.end(),body.begin(),body.end());
  for(unsigned i=0;i<=body.size();++i){tree.anchors.push_back({BlockBoundary{{0},i},{0}});tree.operations.push_back({{i},AnchorId{i}});}
  const auto symbol=[&](SymbolId id){return reverse?SymbolId{static_cast<std::uint32_t>(tree.symbols.size()-1-id.value)}:id;};
  if(reverse)f.Reverse();const auto checked=VerifyTreeStructure(tree);
  if(!std::holds_alternative<TreeStructure>(checked))std::cerr<<"structure expression="<<form<<" count="<<count<<" reverse="<<reverse<<" error="<<static_cast<unsigned>(std::get<TreeStructureError>(checked))<<'\n';
  test::Check(std::holds_alternative<TreeStructure>(checked));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));const auto plan=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  std::ostringstream output;const auto result=PrintFunctionTree(tree,output);test::Check(std::holds_alternative<PrintedFunction>(result));
  const bool folded=count&&form!=1&&form!=3&&form!=5&&form!=7&&form!=8&&form!=9&&form!=14&&form!=18;
  if((plan.aliases[symbol(last).value].has_value()||plan.literals[symbol(last).value].has_value())!=folded)std::cerr<<"expression="<<form<<" count="<<count<<" reverse="<<reverse<<'\n';
  test::Check((plan.aliases[symbol(last).value].has_value()||plan.literals[symbol(last).value].has_value())==folded);
  if(form==18)test::Check(!plan.aliases[symbol(copies.front()).value]);
  for(const auto range:std::get<PrintedFunction>(result).expressions)test::Check(range.begin<range.end&&range.end<=output.str().size());
  for(const auto anchor:std::get<PrintedFunction>(result).anchors)test::Check(anchor>0&&anchor<=output.str().size());
  if(emit)std::cout<<nlohmann::json{{"source",output.str()},{"expression",form},{"count",count},{"folded",folded}}<<'\n';return plan.work;
}
void LoopProjection(bool reverse,bool emit) {
  Expressions f;auto& tree=f.tree;tree.callable=CallableKind::kFunction;tree.strictness=Strictness::kStrict;
  tree.parameter_scope={0};tree.body_scope={1};tree.body={0};tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}},
    {ScopeKind::kBlock,ScopeId{1}},{ScopeKind::kBlock,ScopeId{2}}};tree.names={u"input",u"report",u"other",u"flag",u"saved",u"transport",u"carried"};tree.origins={OriginalSite{{0},{0}}};
  for(unsigned i=0;i<4;++i){tree.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}});tree.parameters.push_back({{i},{},false});}
  for(unsigned i=4;i<7;++i)tree.symbols.push_back({{1},{i},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
  tree.statements.push_back({BlockStmt{{1},0,0},{0}});std::vector<StmtId> body;
  for(unsigned i=4;i<7;++i)body.push_back(f.Stmt(DeclarationStmt{{i},{}}));
  const auto store=[&](unsigned target,ExprId value){return f.Stmt(ExpressionStmt{f.Expr(AssignmentExpr{SymbolTarget{{target}},value})});};
  const auto seed=store(4,f.Call({2},{}));body.push_back(seed);body.push_back(store(5,f.Read({4})));body.push_back(store(6,f.Read({4})));
  const auto loop=f.Stmt(LoopStmt{}),exit_statement=f.Stmt(BreakStmt{loop});tree.children.push_back(exit_statement);
  const auto exit=f.Stmt(BlockStmt{{3},0,1});const auto guard=f.Stmt(IfStmt{f.Expr(UnaryExpr{TreeUnary::kNot,f.Read({6})}),exit,{}});
  tree.children.push_back(guard);tree.children.push_back(store(6,f.Expr(TreeLiteral{BooleanLiteral{false}})));
  std::get<LoopStmt>(tree.statements[loop.value].statement).body=f.Stmt(BlockStmt{{2},1,2});body.push_back(loop);
  body.push_back(f.Stmt(ExpressionStmt{f.Call({1},{f.Read({5})})}));body.push_back(f.Stmt(ReturnStmt{f.Read({5})}));
  std::get<BlockStmt>(tree.statements[0].statement)={{1},static_cast<std::uint32_t>(tree.children.size()),static_cast<std::uint32_t>(body.size())};tree.children.insert(tree.children.end(),body.begin(),body.end());
  for(unsigned i=0;i<=body.size();++i){tree.anchors.push_back({BlockBoundary{{0},i},{0}});tree.operations.push_back({{i},AnchorId{i}});}
  if(reverse)f.Reverse();test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto seed_id=reverse?tree.statements.size()-1-seed.value:seed.value;test::Check(loops.initializer_owners[seed_id].has_value());
  const auto plan=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));std::ostringstream output;test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  test::Check(!plan.aliases[reverse?tree.symbols.size()-1-5:5]);
  if(emit)std::cout<<nlohmann::json{{"source",output.str()},{"expression",20},{"count",1},{"folded",false}}<<'\n';
}
}
void CheckExpressionInitializers(bool emit) {
  for(unsigned form=0;form<18;++form) {
    std::uint64_t small=0,large=0;for(unsigned count:{0U,1U,200U,201U,400U,800U})for(bool reverse:{false,true}) {
      const auto work=Check(form,count,reverse,emit);if(count==400&&!reverse)small=work;if(count==800&&!reverse)large=work;
    }
    test::Check(large<small*3);
  }
  std::uint64_t small=0,large=0;for(unsigned count:{2U,200U,201U,400U,800U})for(bool reverse:{false,true}) {
    const auto work=Check(18,count,reverse,emit);if(count==400&&!reverse)small=work;if(count==800&&!reverse)large=work;
  }
  test::Check(large<small*3);
  small=0;large=0;for(unsigned count:{0U,1U,200U,201U,400U,800U})for(bool reverse:{false,true}) {
    const auto work=Check(19,count,reverse,emit);if(count==400&&!reverse)small=work;if(count==800&&!reverse)large=work;
  }
  test::Check(large<small*3);
  for(bool reverse:{false,true})LoopProjection(reverse,emit);
  for(unsigned form:{21U,22U}) {
    small=0;large=0;for(unsigned count:{0U,1U,200U,201U,400U,800U})for(bool reverse:{false,true}) {
      const auto work=Check(form,count,reverse,emit);if(count==400&&!reverse)small=work;if(count==800&&!reverse)large=work;
    }
    test::Check(large<small*3);
  }
}
