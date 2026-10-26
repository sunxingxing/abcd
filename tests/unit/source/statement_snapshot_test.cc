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
enum class Form { kReturn, kIf, kLoop, kMultiple, kEffect, kArgument, kLocal, kBranchExit,
  kAwait, kYield, kTypeof, kAlias, kShadow, kGuarded, kCatch, kOutsideCatch, kCatchReads, kProjectedLocal, kInterleavedAlias,
  kDeadInitialRead, kObservedInitialRead, kDiscardedOperand, kEffectOperand, kTransparentControl, kDebugger, kOnceLoop, kRepeatedLoop, kRedundantWrites, kEffectWrites, kDeadPrefix, kEffectPrefix, kObservedPrefix, kProjectedPrefix, kProjectedSuffix, kRequiredPrefix, kTdzStore, kUnrelatedBinding, kWrittenShadow, kResourceEnvironment, kFreeNameShadow, kFreeNameUnrelated, kFreeGetter, kThisUnrelated, kThisInputShadow, kThisValue, kNestedReturn, kObservedResult, kReturnPrefix, kTwoSnapshots, kPrefixUnrelated, kPrefixShadow, kComposedEnvironment, kNestedInitializer, kIndependentDestination };
struct Fixture {
  FunctionTree tree{};
  Fixture(Form form) {
    tree.callable=form==Form::kAwait?CallableKind::kAsyncFunction:form==Form::kYield?CallableKind::kGenerator:CallableKind::kFunction;
    tree.strictness=Strictness::kStrict;tree.parameter_scope={0};tree.body_scope={1};tree.body={0};
    tree.scopes={{ScopeKind::kParameters,{}},{ScopeKind::kFunctionBody,ScopeId{0}}};
    tree.names={u"input",u"report",u"flag",u"saved",u"read",u"effect",u"__snapshotMissing"};tree.origins={OriginalSite{{0},{0}}};
    for(unsigned i=0;i<3;++i){tree.symbols.push_back({{0},{i},BindingKind::kParameter,DeclarationKind::kParameter,NameEvidence::kMetadata,{}});tree.parameters.push_back({{i},{},false});}
    tree.symbols.push_back({{1},{3},form==Form::kLocal?BindingKind::kLocal:BindingKind::kCompilerTemporary,
      DeclarationKind::kLet,NameEvidence::kSynthesized,form==Form::kLocal?std::nullopt:std::optional{TemporaryReason::kPredicateSnapshot}});
    tree.statements.push_back({BlockStmt{{1},0,0},{0}});
  }
  ExprId Expr(TreeExpression value){ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};tree.expressions.push_back({std::move(value),{0}});return id;}
  StmtId Stmt(TreeStatement value){StmtId id{static_cast<std::uint32_t>(tree.statements.size())};tree.statements.push_back({std::move(value),{0}});return id;}
  ExprId Read(unsigned symbol){return Expr(SymbolExpr{{symbol}});}
  ExprId Literal(bool value){return Expr(TreeLiteral{BooleanLiteral{value}});}
  ExprId Call(unsigned name){return Expr(CallExpr{Expr(MemberExpr{Read(0),NameId{name}}),{0,0},CallReference::kMember});}
  StmtId Report(std::vector<ExprId> args){const auto first=static_cast<std::uint32_t>(tree.arguments.size());tree.arguments.insert(tree.arguments.end(),args.begin(),args.end());return Stmt(ExpressionStmt{Expr(CallExpr{Read(1),{first,static_cast<std::uint32_t>(args.size())},CallReference::kValue})});}
  StmtId Block(std::vector<StmtId> children,ScopeId parent={1}) {
    const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};tree.scopes.push_back({ScopeKind::kBlock,parent});
    const auto first=static_cast<std::uint32_t>(tree.children.size());tree.children.insert(tree.children.end(),children.begin(),children.end());
    return Stmt(BlockStmt{scope,first,static_cast<std::uint32_t>(children.size())});
  }
  StmtId Wrap(StmtId child,unsigned count) {
    if(!count)return child;
    std::vector<StmtId> siblings;for(unsigned i=1;i<count;++i)siblings.push_back(Block({}));siblings.push_back(child);
    const auto parent=Block(siblings);const auto scope=std::get<BlockStmt>(tree.statements[parent.value].statement).scope;
    for(auto id:siblings)if(auto* block=std::get_if<BlockStmt>(&tree.statements[id.value].statement))tree.scopes[block->scope.value].parent=scope;
    return parent;
  }
  void Finish(std::vector<StmtId> body,bool reverse) {
    auto& root=std::get<BlockStmt>(tree.statements[0].statement);root.first_child=tree.children.size();root.child_count=body.size();tree.children.insert(tree.children.end(),body.begin(),body.end());
    for(unsigned i=0;i<tree.statements.size();++i)if(const auto* block=std::get_if<BlockStmt>(&tree.statements[i].statement))for(unsigned j=0;j<=block->child_count;++j){const auto at=static_cast<std::uint32_t>(tree.anchors.size());tree.anchors.push_back({BlockBoundary{{i},j},{0}});tree.operations.push_back({{at},AnchorId{at}});}
    if(!reverse)return;const auto remap=[&](StmtId id){return StmtId{static_cast<std::uint32_t>(tree.statements.size()-1-id.value)};};
    for(auto& node:tree.statements)std::visit([&](auto& syntax){using T=std::decay_t<decltype(syntax)>;
      if constexpr(std::is_same_v<T,LoopStmt>)syntax.body=remap(syntax.body);
      else if constexpr(std::is_same_v<T,BreakStmt>)syntax.target=remap(syntax.target);
      else if constexpr(std::is_same_v<T,TryStmt>){syntax.body=remap(syntax.body);if(syntax.handler)syntax.handler->body=remap(syntax.handler->body);}
      else if constexpr(std::is_same_v<T,IfStmt>){syntax.consequent=remap(syntax.consequent);if(syntax.alternate)syntax.alternate=remap(*syntax.alternate);}
    },node.statement);
    for(auto& child:tree.children)child=remap(child);for(auto& anchor:tree.anchors)std::get<BlockBoundary>(anchor.position).block=remap(std::get<BlockBoundary>(anchor.position).block);
    tree.body=remap(tree.body);std::reverse(tree.statements.begin(),tree.statements.end());
  }
};
std::uint64_t Check(Form form,unsigned count,bool reverse,bool emit) {
  Fixture f(form);auto& tree=f.tree;const auto declaration=f.Stmt(DeclarationStmt{{3},{}});
  auto rhs=form==Form::kTypeof?f.Expr(GlobalNameExpr{{6}}):f.Call(4);
  if(form==Form::kThisUnrelated||form==Form::kThisInputShadow||form==Form::kThisValue) {
    const auto call=std::get<CallExpr>(tree.expressions[rhs.value].expression);
    const auto base=std::get<MemberExpr>(tree.expressions[call.callee.value].expression).base;
    tree.expressions[base.value].expression=ThisExpr{tree.owner};
    if(form==Form::kThisValue) {
      tree.expressions[call.callee.value].expression=TreeLiteral{BooleanLiteral{false}};
      tree.expressions[rhs.value].expression=SequenceExpr{call.callee,base};
    }
  }
  if(form==Form::kFreeNameShadow||form==Form::kFreeNameUnrelated||form==Form::kFreeGetter) {
    tree.names.push_back(form==Form::kFreeGetter?u"__snapshotFree":u"Math");
    // Keep the original owned callee/base occurrences reachable while replacing
    // their semantics with a composed free-name observation.
    const auto call=std::get<CallExpr>(tree.expressions[rhs.value].expression);
    const auto base=std::get<MemberExpr>(tree.expressions[call.callee.value].expression).base;
    tree.expressions[base.value].expression=GlobalNameExpr{{7}};
    tree.expressions[call.callee.value].expression=UnaryExpr{TreeUnary::kNot,base};
    tree.expressions[rhs.value].expression=UnaryExpr{TreeUnary::kNot,call.callee};
  }
  if(form==Form::kProjectedLocal) {
    tree.names.push_back(u"local");tree.names.push_back(u"value");tree.symbols.push_back({{1},{7},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    // Replace the original root without leaving an unowned expression behind.
    const auto& call=std::get<CallExpr>(tree.expressions[rhs.value].expression);
    const auto base=std::get<MemberExpr>(tree.expressions[call.callee.value].expression).base;
    tree.expressions[base.value].expression=SymbolExpr{{4}};
    tree.expressions[call.callee.value].expression=MemberExpr{base,NameId{8}};
    tree.expressions[rhs.value].expression=UnaryExpr{TreeUnary::kNot,call.callee};
  }
  if(form==Form::kArgument||form==Form::kWrittenShadow)rhs=f.Expr(AssignmentExpr{SymbolTarget{{2}},rhs});
  if(form==Form::kAwait)rhs=f.Expr(AwaitExpr{rhs});if(form==Form::kYield)rhs=f.Expr(YieldExpr{rhs});
  auto stored=f.Expr(AssignmentExpr{SymbolTarget{{3}},rhs});
  if(form==Form::kProjectedPrefix||form==Form::kProjectedSuffix||form==Form::kRequiredPrefix||form==Form::kReturnPrefix||form==Form::kPrefixUnrelated||form==Form::kPrefixShadow)
    for(unsigned i=0;i<=count;++i) {
      ExprId operand;
      if(form==Form::kPrefixUnrelated) {
        const auto first=static_cast<std::uint32_t>(tree.arguments.size());tree.arguments.push_back(f.Read(2));
        operand=f.Expr(CallExpr{f.Read(1),{first,1},CallReference::kValue});
      } else operand=form==Form::kRequiredPrefix||form==Form::kReturnPrefix||form==Form::kPrefixShadow?f.Call(5):f.Literal(false);
      stored=f.Expr(form==Form::kProjectedSuffix?SequenceExpr{stored,operand}:SequenceExpr{operand,stored});
    }
  const auto store=f.Stmt(ExpressionStmt{stored});
  std::vector<StmtId> body{declaration,f.Wrap(store,count)};unsigned symbol=3;
  if(form==Form::kDeadPrefix||form==Form::kEffectPrefix||form==Form::kObservedPrefix) {
    std::vector<StmtId> prefix{f.Stmt(ExpressionStmt{f.Read(3)})};
    for(unsigned i=0;i<=count;++i)prefix.push_back(f.Stmt(ExpressionStmt{f.Expr(AssignmentExpr{
        SymbolTarget{{3}},form==Form::kEffectPrefix?f.Call(5):f.Read(2)})}));
    if(form==Form::kObservedPrefix)prefix.push_back(f.Report({f.Read(3)}));
    body.insert(body.begin()+1,prefix.begin(),prefix.end());
  }
  if(form==Form::kDeadInitialRead || form==Form::kObservedInitialRead)
    body.insert(body.begin()+1,form==Form::kDeadInitialRead?
        f.Stmt(ExpressionStmt{f.Read(3)}):f.Report({f.Read(3)}));
  if(form==Form::kAlias||form==Form::kInterleavedAlias) {
    // Copies are independent register/storage variations, not new match rules.
    for(unsigned i=0;i<=count;++i){const auto next=static_cast<unsigned>(tree.symbols.size());const auto name=static_cast<unsigned>(tree.names.size());const auto text="copy"+std::to_string(i);tree.names.emplace_back(text.begin(),text.end());tree.symbols.push_back({{1},{name},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
      if(form==Form::kInterleavedAlias)body.push_back(f.Stmt(DeclarationStmt{{next},{}}));else body.insert(body.begin(),f.Stmt(DeclarationStmt{{next},{}}));body.push_back(f.Stmt(ExpressionStmt{f.Expr(AssignmentExpr{SymbolTarget{{next}},f.Read(symbol)})}));symbol=next;}
  }
  if(form==Form::kRedundantWrites||form==Form::kEffectWrites)for(unsigned i=0;i<=count;++i) {
    auto value=f.Read(3);
    if(form==Form::kEffectWrites)value=f.Expr(SequenceExpr{f.Call(5),value});
    body.push_back(f.Stmt(ExpressionStmt{f.Expr(AssignmentExpr{SymbolTarget{{3}},value})}));
  }
  if(form==Form::kMultiple)body.push_back(f.Report({f.Read(3)}));
  if(form==Form::kEffect)body.push_back(f.Stmt(ExpressionStmt{f.Call(5)}));
  if(form==Form::kBranchExit)body.push_back(f.Stmt(IfStmt{f.Literal(true),f.Block({f.Stmt(ReturnStmt{f.Literal(false)})}),{}}));
  if(form==Form::kTransparentControl)body.push_back(f.Stmt(IfStmt{f.Literal(false),f.Block({}),f.Block({})}));
  if(form==Form::kDebugger)body.push_back(f.Stmt(DebuggerStmt{}));
  const auto read=f.Read(symbol);auto result=read;
  if(form==Form::kDiscardedOperand||form==Form::kEffectOperand)
    for(unsigned i=0;i<=count;++i)result=f.Expr(SequenceExpr{form==Form::kDiscardedOperand?f.Read(2):f.Call(5),result});
  if(form==Form::kTypeof)result=f.Expr(UnaryExpr{TreeUnary::kTypeof,read});
  std::optional<ExprId> second_read,second_rhs;std::optional<StmtId> second_declaration;
  if(form==Form::kNestedReturn||form==Form::kReturnPrefix||form==Form::kObservedResult||form==Form::kTwoSnapshots) {
    auto combined=form==Form::kObservedResult?f.Expr(BinaryExpr{analysis::BinaryOperator::kStrictEqual,stored,read}):f.Expr(SequenceExpr{stored,read});
    if(form==Form::kTwoSnapshots) {
      const auto name=static_cast<std::uint32_t>(tree.names.size());tree.names.push_back(u"second");
      const auto second=static_cast<std::uint32_t>(tree.symbols.size());
      tree.symbols.push_back({{1},{name},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kPredicateSnapshot});
      body.insert(body.begin(),f.Stmt(DeclarationStmt{{second},{}}));
      second_rhs=f.Call(5);const auto write=f.Expr(AssignmentExpr{SymbolTarget{{second}},*second_rhs});second_read=f.Read(second);
      combined=f.Expr(BinaryExpr{analysis::BinaryOperator::kStrictEqual,combined,f.Expr(SequenceExpr{write,*second_read})});
    }
    tree.statements[store.value].statement=ReturnStmt{combined};
  } else if(form==Form::kIf)body.push_back(f.Stmt(IfStmt{read,f.Block({f.Stmt(ReturnStmt{f.Literal(true)})}),f.Block({f.Stmt(ReturnStmt{f.Literal(false)})})}));
  else if(form==Form::kOnceLoop || form==Form::kRepeatedLoop) {
    const auto loop=f.Stmt(LoopStmt{});std::vector<StmtId> inside;
    if(form==Form::kOnceLoop)inside.push_back(f.Stmt(ReturnStmt{read}));
    else {
      const auto exit=f.Block({f.Stmt(BreakStmt{loop})});
      const auto stop=f.Block({f.Stmt(BreakStmt{loop})});
      inside={f.Stmt(IfStmt{read,exit,{}}),f.Report({}),f.Stmt(IfStmt{f.Read(2),stop,{}}),
        f.Stmt(ExpressionStmt{f.Expr(AssignmentExpr{SymbolTarget{{2}},f.Literal(true)})})};
      const auto owned=f.Block(inside);const auto scope=std::get<BlockStmt>(tree.statements[owned.value].statement).scope;
      for(const auto branch:{exit,stop})tree.scopes[std::get<BlockStmt>(tree.statements[branch.value].statement).scope.value].parent=scope;
      std::get<LoopStmt>(tree.statements[loop.value].statement).body=owned;
    }
    if(form==Form::kOnceLoop)std::get<LoopStmt>(tree.statements[loop.value].statement).body=f.Block(inside);
    body.push_back(loop);body.push_back(f.Stmt(ReturnStmt{f.Literal(false)}));
  }
  else if(form==Form::kLoop) {
    const auto loop=f.Stmt(LoopStmt{});const auto exit=f.Block({f.Stmt(BreakStmt{loop})});
    const auto guard=f.Stmt(IfStmt{f.Expr(UnaryExpr{TreeUnary::kNot,read}),exit,{}});
    const auto loop_body=f.Block({body.back(),guard,f.Report({})});
    auto& block=std::get<BlockStmt>(tree.statements[loop_body.value].statement);tree.scopes[std::get<BlockStmt>(tree.statements[exit.value].statement).scope.value].parent=block.scope;
    if(auto* wrapped=std::get_if<BlockStmt>(&tree.statements[body.back().value].statement))tree.scopes[wrapped->scope.value].parent=block.scope;
    std::get<LoopStmt>(tree.statements[loop.value].statement).body=loop_body;body.back()=loop;body.push_back(f.Stmt(ReturnStmt{f.Literal(false)}));
  } else if(form==Form::kArgument) {body.push_back(f.Report({f.Read(2),read}));body.push_back(f.Stmt(ReturnStmt{f.Literal(false)}));}
  else if(form==Form::kGuarded)body.push_back(f.Stmt(IfStmt{f.Literal(false),f.Block({f.Stmt(ReturnStmt{read})}),{}}));
  else if(form==Form::kShadow||form==Form::kUnrelatedBinding||form==Form::kWrittenShadow||form==Form::kResourceEnvironment||form==Form::kFreeNameShadow||form==Form::kFreeNameUnrelated||form==Form::kFreeGetter||form==Form::kThisUnrelated||form==Form::kThisInputShadow||form==Form::kThisValue||form==Form::kPrefixUnrelated||form==Form::kPrefixShadow) {
    const auto inner=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[inner.value].statement).scope;
    const auto shadow=static_cast<unsigned>(tree.symbols.size());tree.symbols.push_back({scope,{(form==Form::kShadow||form==Form::kThisInputShadow||form==Form::kPrefixShadow)?0U:form==Form::kFreeNameShadow?7U:2U},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    auto& block=std::get<BlockStmt>(tree.statements[inner.value].statement);block.first_child=tree.children.size();block.child_count=2;tree.children.push_back(f.Stmt(ReturnStmt{read}));tree.children.push_back(f.Stmt(DeclarationStmt{{shadow},f.Literal(false)}));body.push_back(inner);
  } else body.push_back(f.Stmt(ReturnStmt{result}));
  if(form==Form::kIndependentDestination) {
    const auto name=static_cast<std::uint32_t>(tree.names.size());tree.names.push_back(u"second");
    const auto second=static_cast<std::uint32_t>(tree.symbols.size());
    tree.symbols.push_back({{1},{name},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kPredicateSnapshot});
    body.insert(body.begin(),f.Stmt(DeclarationStmt{{second},{}}));
    second_rhs=f.Expr(CallExpr{f.Read(1),{0,0},CallReference::kValue});second_read=f.Read(second);
    const auto written=f.Expr(AssignmentExpr{SymbolTarget{{second}},*second_rhs});
    std::get<ReturnStmt>(tree.statements[body.back().value].statement).value=f.Expr(BinaryExpr{
        analysis::BinaryOperator::kStrictEqual,read,f.Expr(SequenceExpr{written,*second_read})});
    const auto inner=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[inner.value].statement).scope;
    const auto shadow=static_cast<std::uint32_t>(tree.symbols.size());
    tree.symbols.push_back({scope,{0},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    auto& block=std::get<BlockStmt>(tree.statements[inner.value].statement);
    block.first_child=tree.children.size();block.child_count=2;
    tree.children.push_back(body.back());tree.children.push_back(f.Stmt(DeclarationStmt{{shadow},f.Literal(false)}));body.back()=inner;
  }
  if(form==Form::kNestedInitializer) {
    const auto name=static_cast<std::uint32_t>(tree.names.size());tree.names.push_back(u"second");
    const auto second=static_cast<std::uint32_t>(tree.symbols.size());
    tree.symbols.push_back({{1},{name},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
    second_declaration=f.Stmt(DeclarationStmt{{second},{}});body.insert(body.begin(),*second_declaration);
    const auto value=f.Expr(SequenceExpr{stored,f.Expr(UnaryExpr{TreeUnary::kPlus,read})});
    std::get<ExpressionStmt>(tree.statements[store.value].statement).expression=f.Expr(AssignmentExpr{SymbolTarget{{second}},value});
    second_read=f.Read(second);std::get<ReturnStmt>(tree.statements[body.back().value].statement).value=*second_read;
    body.insert(body.end()-1,f.Report({f.Read(second)}));
  }
  if(form==Form::kComposedEnvironment) {
    const auto name=static_cast<std::uint32_t>(tree.names.size());tree.names.push_back(u"second");
    const auto second=static_cast<std::uint32_t>(tree.symbols.size());
    tree.symbols.push_back({{1},{name},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kPredicateSnapshot});
    second_read=f.Read(second);second_rhs=rhs;
    const auto transformed=f.Expr(UnaryExpr{TreeUnary::kNegate,*second_read});
    std::get<AssignmentExpr>(tree.expressions[stored.value].expression).value=transformed;
    const auto loop=f.Stmt(LoopStmt{}),inner=f.Block({});
    const auto scope=std::get<BlockStmt>(tree.statements[inner.value].statement).scope;
    const auto shadow=static_cast<std::uint32_t>(tree.symbols.size());
    tree.symbols.push_back({scope,{0},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
    const auto wrapped=body[1];
    if(auto* block=std::get_if<BlockStmt>(&tree.statements[wrapped.value].statement))tree.scopes[block->scope.value].parent=scope;
    auto& block=std::get<BlockStmt>(tree.statements[inner.value].statement);
    block.first_child=tree.children.size();block.child_count=3;
    tree.children.push_back(wrapped);tree.children.push_back(f.Stmt(BreakStmt{loop}));
    tree.children.push_back(f.Stmt(DeclarationStmt{{shadow},f.Literal(false)}));
    std::get<LoopStmt>(tree.statements[loop.value].statement).body=inner;
    body={declaration,f.Stmt(DeclarationStmt{{second},{}}),f.Stmt(ExpressionStmt{f.Expr(AssignmentExpr{SymbolTarget{{second}},rhs})}),loop,body.back()};
  }
  if(form==Form::kTdzStore) {body.erase(body.begin());body.push_back(declaration);}
  if(form==Form::kProjectedLocal) {
    const auto initialize=f.Stmt(ExpressionStmt{f.Expr(AssignmentExpr{SymbolTarget{{4}},f.Call(4)})});
    const auto inside=f.Block({initialize,body[1]});
    if(auto* wrapped=std::get_if<BlockStmt>(&tree.statements[body[1].value].statement))tree.scopes[wrapped->scope.value].parent=std::get<BlockStmt>(tree.statements[inside.value].statement).scope;
    body={declaration,f.Stmt(DeclarationStmt{{4},{}}),inside,body.back()};
  }
  if(form==Form::kCatch||form==Form::kOutsideCatch||form==Form::kCatchReads) {
    const auto caught=f.Block({});const auto scope=std::get<BlockStmt>(tree.statements[caught.value].statement).scope;tree.scopes[scope.value].kind=ScopeKind::kCatch;
    const auto error=static_cast<unsigned>(tree.symbols.size()),name=static_cast<unsigned>(tree.names.size());tree.names.push_back(u"error");
    tree.symbols.push_back({scope,{name},BindingKind::kCatchParameter,DeclarationKind::kCatchParameter,NameEvidence::kSynthesized,{}});
    std::vector<StmtId> handler;if(form==Form::kCatchReads)handler.push_back(f.Report({f.Read(3)}));handler.push_back(f.Stmt(ReturnStmt{f.Read(error)}));
    auto& block=std::get<BlockStmt>(tree.statements[caught.value].statement);block.first_child=tree.children.size();block.child_count=handler.size();tree.children.insert(tree.children.end(),handler.begin(),handler.end());
    std::vector<StmtId> inside{body[1]};if(form!=Form::kOutsideCatch)inside.push_back(body.back());const auto protected_body=f.Block(inside);
    if(auto* wrapped=std::get_if<BlockStmt>(&tree.statements[body[1].value].statement))tree.scopes[wrapped->scope.value].parent=std::get<BlockStmt>(tree.statements[protected_body.value].statement).scope;
    const auto tried=f.Stmt(TryStmt{protected_body,CatchClause{{error},caught},{}});
    if(form==Form::kOutsideCatch)body={declaration,tried,body.back()};else body={declaration,tried};
  }
  f.Finish(body,reverse);test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));const auto plan=std::get<BindingInitializers>(PlanBindingInitializers(tree,loops,nullish));
  // Snapshot forwarding precedes storage analysis. Final facts must describe
  // the forwarded expression, including effects newly present at a read site.
  for (std::size_t i=0;i<tree.expressions.size();++i) if (const auto replacement=plan.replacements[i]) {
    test::Check(!plan.normal_stores[i]);
    if (plan.evaluations[replacement->value]==ExpressionEvaluation::kRequired)
      test::Check(plan.evaluations[i]==ExpressionEvaluation::kRequired);
  }

  // Propagating the observed prefix's immutable value enables a later motion
  // round. Its report remains before the producer; JS verifies both traces.
  const bool folded=form==Form::kObservedPrefix||form==Form::kReturn||form==Form::kIf||form==Form::kLoop||form==Form::kAwait||form==Form::kYield||form==Form::kTypeof||form==Form::kAlias||form==Form::kCatch||form==Form::kCatchReads||form==Form::kObservedInitialRead||form==Form::kInterleavedAlias||form==Form::kDeadInitialRead||form==Form::kDiscardedOperand||form==Form::kTransparentControl||form==Form::kOnceLoop||form==Form::kRedundantWrites||form==Form::kDeadPrefix||form==Form::kEffectPrefix||form==Form::kProjectedPrefix||form==Form::kProjectedSuffix||form==Form::kRequiredPrefix||form==Form::kUnrelatedBinding||form==Form::kFreeNameUnrelated||form==Form::kFreeGetter||form==Form::kThisUnrelated||form==Form::kThisInputShadow||form==Form::kThisValue||form==Form::kNestedReturn||form==Form::kReturnPrefix||form==Form::kTwoSnapshots||form==Form::kPrefixUnrelated||form==Form::kNestedInitializer||form==Form::kResourceEnvironment||form==Form::kComposedEnvironment;
  if(plan.replacements[read.value].has_value()!=folded)std::cerr<<"form="<<static_cast<unsigned>(form)<<" count="<<count<<" reverse="<<reverse<<'\n';
  test::Check(plan.replacements[read.value].has_value()==folded);
  if(second_read)test::Check(plan.replacements[second_read->value].has_value()==(form==Form::kTwoSnapshots||form==Form::kIndependentDestination||form==Form::kComposedEnvironment));
  if(second_declaration) {
    const auto placed=reverse?StmtId{static_cast<std::uint32_t>(tree.statements.size()-1-store.value)}:store;
    const auto original=reverse?StmtId{static_cast<std::uint32_t>(tree.statements.size()-1-second_declaration->value)}:*second_declaration;
    test::Check(plan.statements[placed.value].has_value()||!plan.omitted[original.value]);
  }
  const bool aliased=form==Form::kArgument;
  if(aliased)test::Check(plan.aliases[3]==SymbolId{2});
  std::ostringstream output;const auto printed=PrintFunctionTree(tree,output);test::Check(std::holds_alternative<PrintedFunction>(printed));
  const auto& ranges=std::get<PrintedFunction>(printed).expressions;
  for(const auto range:ranges) {
    test::Check(range.begin<=range.end&&range.end<=output.str().size());
    if(form==Form::kProjectedPrefix||form==Form::kProjectedSuffix||form==Form::kRequiredPrefix)test::Check(range.begin>0);
  }
  const auto& positions=std::get<PrintedFunction>(printed);
  test::Check(positions.expression_extents.size()==ranges.size());
  for(std::size_t i=0;i<ranges.size();++i) {
    if ((positions.expression_extents[i]==GeneratedExtent::kBoundary)!=(ranges[i].begin==ranges[i].end))
      std::cerr << "extent form=" << static_cast<unsigned>(form) << " count=" << count << " reverse=" << reverse << " expression=" << i
                << " range=" << ranges[i].begin << ',' << ranges[i].end << '\n';
    test::Check((positions.expression_extents[i]==GeneratedExtent::kBoundary)==(ranges[i].begin==ranges[i].end));
  }
  if(folded)test::Check(ranges[rhs.value].begin<ranges[rhs.value].end);
  if(folded)test::Check(positions.expression_extents[rhs.value]==GeneratedExtent::kSpan);
  if(second_rhs)test::Check(positions.expression_extents[second_rhs->value]==GeneratedExtent::kSpan);
  if(emit)std::cout<<nlohmann::json{{"source",output.str()},{"form",form},{"count",count},{"folded",folded},{"aliased",aliased}}<<'\n';return plan.work;
}
// Restoring one rejected move reactivates its shadow for another independent
// move. A proof for the all-elided draft cannot certify that final subset.
std::uint64_t RestoredShadow(unsigned count,bool reverse,bool emit) {
  Fixture f(Form::kReturn);auto& tree=f.tree;
  const auto outer=f.Block({}),inner=f.Block({});
  const auto outer_scope=std::get<BlockStmt>(tree.statements[outer.value].statement).scope;
  const auto inner_scope=std::get<BlockStmt>(tree.statements[inner.value].statement).scope;
  tree.scopes[inner_scope.value].parent=outer_scope;
  const SymbolId shadow{4},barrier{5};
  tree.symbols.push_back({outer_scope,{0},BindingKind::kCompilerTemporary,DeclarationKind::kLet,NameEvidence::kSynthesized,TemporaryReason::kMultipleUses});
  tree.symbols.push_back({inner_scope,{1},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kMetadata,{}});
  const auto saved=f.Stmt(DeclarationStmt{{3},{}}),decl=f.Stmt(DeclarationStmt{shadow,{}});
  const auto store=f.Stmt(ExpressionStmt{f.Expr(AssignmentExpr{SymbolTarget{{3}},f.Expr(UnaryExpr{TreeUnary::kNot,f.Read(0)})})});
  const auto write=f.Stmt(ExpressionStmt{f.Expr(AssignmentExpr{SymbolTarget{shadow},f.Expr(UnaryExpr{TreeUnary::kNot,f.Read(1)})})});
  const auto saved_read=f.Read(3),shadow_read=f.Read(4);
  const auto result=f.Stmt(ReturnStmt{f.Expr(BinaryExpr{analysis::BinaryOperator::kStrictEqual,saved_read,shadow_read})});
  const auto retained=f.Stmt(DeclarationStmt{barrier,f.Literal(false)});
  std::get<BlockStmt>(tree.statements[inner.value].statement)={inner_scope,static_cast<unsigned>(tree.children.size()),2};
  tree.children.insert(tree.children.end(),{result,retained});
  std::get<BlockStmt>(tree.statements[outer.value].statement)={outer_scope,static_cast<unsigned>(tree.children.size()),3};
  tree.children.insert(tree.children.end(),{decl,write,inner});
  f.Finish({saved,store,f.Wrap(outer,count)},reverse);
  test::Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));
  const auto nullish=std::get<NullishSyntax>(PlanNullishSyntax(tree));
  const auto analyzed=PlanBindingInitializers(tree,loops,nullish);
  if(const auto* error=std::get_if<TreeStructureError>(&analyzed))std::cerr<<"restored shadow error="<<static_cast<unsigned>(*error)<<'\n';
  test::Check(std::holds_alternative<BindingInitializers>(analyzed));
  const auto& plan=std::get<BindingInitializers>(analyzed);
  test::Check(!plan.replacements[saved_read.value]&&!plan.replacements[shadow_read.value]);
  std::ostringstream output;test::Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(tree,output)));
  if(emit)std::cout<<nlohmann::json{{"source",output.str()},{"form",54},{"count",count},{"folded",false},{"aliased",false}}<<'\n';
  return plan.work;
}

}
int main(int argc,char** argv) {
  if(argc==2&&std::string_view(argv[1])=="--retired-shadow") {
    for(unsigned count:{0U,1U,200U,201U,400U,800U})for(bool reverse:{false,true})RestoredShadow(count,reverse,true);
    return 0;
  }
  std::uint64_t small=0,large=0;
  for(unsigned count:{0U,1U,200U,201U,400U,800U})for(bool reverse:{false,true}) {
    const auto work=RestoredShadow(count,reverse,argc>1);
    if(count==400&&!reverse)small=work;if(count==800&&!reverse)large=work;
  }
  test::Check(large<small*3);
  for(unsigned form=0;form<=static_cast<unsigned>(Form::kIndependentDestination);++form){std::uint64_t small=0,large=0;
    for(unsigned count:{0U,1U,200U,201U,400U,800U})for(bool reverse:{false,true}){const auto work=Check(static_cast<Form>(form),count,reverse,argc>1);if(count==400&&!reverse)small=work;if(count==800&&!reverse)large=work;}
    test::Check(large<small*3);
  }
}
