#include "expression_initializer_checks.h"
#include "../../../src/source/binding_initializer.h"
#include "../../../src/source/expression_children.h"
#include "../../../src/source/loop_syntax.h"
#include "../../../src/source/nullish_syntax.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
#include <nlohmann/json.hpp>
#include <sstream>
using namespace abcd;
using namespace abcd::source;
namespace {
void CheckExpressionRanges(const FunctionTree& tree,const BindingInitializers& plan,
    const PrintedFunction& printed,std::size_t size) {
  std::vector<std::optional<std::uint64_t>> omitted(tree.expressions.size());
  for(std::size_t i=0;i<tree.statements.size();++i) {
    const auto* expression=std::get_if<ExpressionStmt>(&tree.statements[i].statement);
    if(!expression||!plan.omitted[i])continue;
    std::vector<ExprId> pending{expression->expression};
    while(!pending.empty()) {
      const auto id=pending.back();pending.pop_back();
      if(plan.forwarded_values[i]==id)continue;
      omitted[id.value]=printed.statements[i].begin;
      VisitExpressionChildren(tree,tree.expressions[id.value].expression,[&](ExprId child){pending.push_back(child);});
    }
  }
  for(std::size_t i=0;i<printed.expressions.size();++i) {
    const auto range=printed.expressions[i];test::Check(range.begin>0&&range.end<=size);
    if(printed.expression_extents[i]==GeneratedExtent::kBoundary) {
      // A deleted whole statement has no replacement text. Its owned origins
      // must map to that exact omission boundary, not an arbitrary empty span.
      test::Check(omitted[i]&&range.begin==*omitted[i]&&range.begin==range.end);
    } else test::Check(range.begin<range.end);
  }
}
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
      else if constexpr(std::is_same_v<T,SequenceExpr>||std::is_same_v<T,LogicalExpr>||std::is_same_v<T,BinaryExpr>){value.left=expression(value.left);value.right=expression(value.right);}
      else if constexpr(std::is_same_v<T,MemberExpr>){value.base=expression(value.base);if(auto* key=std::get_if<ComputedKey>(&value.key))key->expression=expression(key->expression);}
      else if constexpr(std::is_same_v<T,ConditionalExpr>){value.condition=expression(value.condition);value.consequent=expression(value.consequent);value.alternate=expression(value.alternate);}
      else if constexpr(std::is_same_v<T,AwaitExpr>||std::is_same_v<T,YieldExpr>||std::is_same_v<T,UnaryExpr>)value.operand=expression(value.operand);
      else test::Check(std::holds_alternative<TreeLiteral>(node.expression)||std::holds_alternative<EmptyObjectExpr>(node.expression)||std::holds_alternative<GlobalNameExpr>(node.expression)||std::holds_alternative<ThisExpr>(node.expression));
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
    for(auto& parameter:tree.parameters){parameter.symbol=symbol(parameter.symbol);if(parameter.initializer)parameter.initializer=expression(*parameter.initializer);}
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
  const bool folded=count&&form!=1&&form!=3&&form!=5&&form!=9&&form!=14&&form!=18;
  // The self-copy has a live initialized undefined value. The common value
  // proof replaces reads; independent dead-definition/lifetime passes may
  // then remove storage without deleting an observable initialization.
  const bool shared=folded;
  if((plan.aliases[symbol(last).value].has_value()||plan.literals[symbol(last).value].has_value())!=shared)std::cerr<<"expression="<<form<<" count="<<count<<" reverse="<<reverse<<'\n';
  test::Check((plan.aliases[symbol(last).value].has_value()||plan.literals[symbol(last).value].has_value())==shared);
  if(form==18)test::Check(!plan.aliases[symbol(copies.front()).value]);
  CheckExpressionRanges(tree,plan,std::get<PrintedFunction>(result),output.str().size());
  for(const auto anchor:std::get<PrintedFunction>(result).anchors)test::Check(anchor>0&&anchor<=output.str().size());
  if(emit)std::cout<<nlohmann::json{{"source",output.str()},{"expression",form},{"count",count},{"folded",folded}}<<'\n';return plan.work;
}
std::uint64_t Sequence(unsigned form,unsigned count,bool reverse,bool emit) {
  Expressions f;auto& tree=f.tree;tree.callable=form==14?CallableKind::kAsyncFunction:form==15?CallableKind::kGenerator:CallableKind::kFunction;
  tree.strictness=Strictness::kStrict;tree.parameter_scope={0};tree.body_scope={1};tree.body={0};
  tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
  tree.names={u"input",u"report",u"other",u"flag",u"state",u"value",u"__sequenceMissing"};tree.origins={OriginalSite{{0},{0}}};
  for(unsigned i=0;i<4;++i){tree.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}});tree.parameters.push_back({{i},{},false});}
  tree.statements.push_back({BlockStmt{{1},0,0},{0}});std::vector<StmtId> body;
  if(form==12||form==13)tree.symbols.push_back({{1},{4},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
  if(form==12)body.push_back(f.Stmt(DeclarationStmt{{4},f.Read({0})}));
  if(form==19)tree.parameters[0].initializer=f.Expr(SequenceExpr{f.Read({0}),f.Expr(TreeLiteral{IntegerLiteral{1}})});
  auto nested=f.Read({0});std::optional<ExprId> probe;
  for(unsigned i=0;i<count;++i) {
    ExprId left;
    switch(form) {
      case 0:case 8:case 19:left=f.Expr(TreeLiteral{IntegerLiteral{0}});break;
      case 1:left=f.Read({0});break;
      case 2:case 3:case 6:left=f.Expr(UnaryExpr{form==2?TreeUnary::kNot:form==3?TreeUnary::kTypeof:TreeUnary::kNegate,f.Read({0})});break;
      case 4:case 5:left=f.Expr(BinaryExpr{form==4?analysis::BinaryOperator::kStrictEqual:analysis::BinaryOperator::kEqual,f.Read({0}),f.Read({3})});break;
      case 7:left=f.Expr(MemberExpr{f.Read({0}),NameId{5}});break;
      case 9:left=f.Expr(ThisExpr{tree.owner});break;
      case 10:left=f.Expr(LogicalExpr{TreeLogical::kAnd,f.Read({0}),f.Read({3})});break;
      case 11:left=f.Expr(ConditionalExpr{f.Read({3}),f.Read({0}),f.Read({2})});break;
      case 12:case 13:left=f.Read({4});break;
      case 14:left=f.Expr(AwaitExpr{f.Call({2},{})});break;
      case 15:left=f.Expr(YieldExpr{f.Call({2},{})});break;
      case 16:left=f.Expr(EmptyObjectExpr{});break;
      case 17:left=f.Call({2},{});break;
      case 18:left=f.Expr(GlobalNameExpr{{6}});break;
      default:test::Check(false);
    }
    if(!probe)probe=left;
    nested=f.Expr(SequenceExpr{left,nested});
  }
  if(form==8)nested=f.Expr(UnaryExpr{TreeUnary::kTypeof,f.Expr(SequenceExpr{nested,f.Expr(GlobalNameExpr{{6}})})});
  body.push_back(f.Stmt(ExpressionStmt{f.Call({1},{nested})}));
  if(form==13)body.push_back(f.Stmt(DeclarationStmt{{4},f.Read({0})}));
  body.push_back(f.Stmt(ReturnStmt{f.Read({0})}));
  std::get<BlockStmt>(tree.statements[0].statement)={{1},0,static_cast<std::uint32_t>(body.size())};tree.children=body;
  for(unsigned i=0;i<=body.size();++i){tree.anchors.push_back({BlockBoundary{{0},i},{0}});tree.operations.push_back({{i},AnchorId{i}});}
  if(probe&&reverse)probe=ExprId{static_cast<std::uint32_t>(tree.expressions.size()-1-probe->value)};
  if(reverse)f.Reverse();const auto checked=VerifyTreeStructure(tree);
  if(form==19){test::Check(std::get<TreeStructureError>(checked)==TreeStructureError::kUnsupportedSyntax);return 0;}
  if(!std::holds_alternative<TreeStructure>(checked))std::cerr<<"sequence="<<form<<" count="<<count<<" reverse="<<reverse<<" structure="<<static_cast<unsigned>(std::get<TreeStructureError>(checked))<<'\n';
  test::Check(std::holds_alternative<TreeStructure>(checked));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto plan=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  const bool inert=form<=4||form==8||form==9||form==10||form==11||form==12||form==19;
  if(probe)test::Check((plan.evaluations[probe->value]==ExpressionEvaluation::kInert)==inert);
  std::ostringstream output;const auto printed=PrintFunctionTree(tree,output);test::Check(std::holds_alternative<PrintedFunction>(printed));
  CheckExpressionRanges(tree,plan,std::get<PrintedFunction>(printed),output.str().size());
  if(emit)std::cout<<nlohmann::json{{"source",output.str()},{"sequence",form},{"count",count},{"inert",inert}}<<'\n';
  return plan.work;
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
  Sequence(19,0,false,false);
  for(unsigned form=0;form<19;++form) {
    std::uint64_t small=0,large=0;for(unsigned count:{0U,1U,200U,201U,400U,800U})for(bool reverse:{false,true}) {
      const auto work=Sequence(form,count,reverse,emit);if(count==400&&!reverse)small=work;if(count==800&&!reverse)large=work;
    }
    test::Check(large<small*3);
  }
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
