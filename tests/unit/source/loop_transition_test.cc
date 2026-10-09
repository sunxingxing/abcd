#include "../../../src/source/loop_syntax.h"
#include "../../../src/source/recovery_flow.h"
#include "../../../src/source/flow_dominance.h"
#include "../../../src/source/statement_children.h"
#include "abcd/source/tree_printer.h"
#include "../../support/analysis/ssa_fixture.h"
#include <algorithm>
#include <array>
#include <sstream>
#include <nlohmann/json.hpp>
using namespace abcd;
using namespace abcd::source;
namespace {
struct Fixture {
  FunctionTree tree{}; StmtId loop{1};
  ExprId Expr(TreeExpression node) {
    const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())}; tree.expressions.push_back({std::move(node), {0}}); return id;
  }
  StmtId Stmt(TreeStatement node) {
    const StmtId id{static_cast<std::uint32_t>(tree.statements.size())}; tree.statements.push_back({std::move(node), {0}}); return id;
  }
  ScopeId Scope(ScopeId parent, ScopeKind kind = ScopeKind::kBlock) {
    const ScopeId id{static_cast<std::uint32_t>(tree.scopes.size())}; tree.scopes.push_back({kind, parent}); return id;
  }
  StmtId Block(ScopeId scope, const std::vector<StmtId>& children) {
    const auto first = static_cast<std::uint32_t>(tree.children.size()); tree.children.insert(tree.children.end(), children.begin(), children.end());
    return Stmt(BlockStmt{scope, first, static_cast<std::uint32_t>(children.size()), {}});
  }
  ExprId Read(unsigned symbol) { return Expr(SymbolExpr{{symbol}}); }
  ExprId Call(unsigned symbol, bool argument = false) {
    const auto first = static_cast<std::uint32_t>(tree.arguments.size()); if (argument) tree.arguments.push_back(Read(5));
    return Expr(CallExpr{Read(symbol), {first, argument ? 1U : 0U}, CallReference::kValue});
  }
  StmtId Store(unsigned symbol, ExprId value) { return Stmt(ExpressionStmt{Expr(AssignmentExpr{SymbolTarget{{symbol}}, value})}); }
  Fixture(unsigned form, unsigned count, bool reverse) {
    tree.callable = form == 5 ? CallableKind::kAsyncFunction : form == 6 ? CallableKind::kGenerator : CallableKind::kFunction;
    tree.strictness = Strictness::kStrict; tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
    tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
    tree.names = {u"initial", u"step", u"report", u"mark", u"skip", u"state", u"next", u"error", u"local"};
    tree.origins = {OriginalSite{{0}, {0}}};
    for (unsigned i = 0; i < 5; ++i) {
      tree.symbols.push_back({{0}, {i}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}});
      tree.parameters.push_back({{i}, {}, false});
    }
    for (unsigned i : {5U, 6U}) tree.symbols.push_back({{1}, {i}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kSynthesized, {}});
    tree.statements = {{BlockStmt{{1}, 0, 0, {}}, {0}}, {LoopStmt{{0}, {}}, {0}}};
    const auto protected_scope = Scope({1}), scope = Scope(protected_scope);
    std::vector<StmtId> body{Stmt(IfStmt{Expr(UnaryExpr{TreeUnary::kNot, Read(5)}), Block(Scope(scope), {Stmt(BreakStmt{loop, {}})}), {}})};
    if(form>=9) {
      StmtId retained;
      if(form==11)retained=Stmt(ReturnStmt{Read(5)});
      else {
        const auto retained_scope=Scope(scope);
        const SymbolId local{static_cast<std::uint32_t>(tree.symbols.size())};
        tree.symbols.push_back({retained_scope,{8},BindingKind::kLocal,DeclarationKind::kLet,NameEvidence::kSynthesized,{}});
        retained=Block(retained_scope,{Stmt(DeclarationStmt{local,Read(5)}),Stmt(ExpressionStmt{Call(2,true)})});
        if(form==10)retained=Stmt(TryStmt{retained,{},Block(Scope(scope),{Stmt(ExpressionStmt{Call(3)})})});
      }
      std::get<IfStmt>(tree.statements[body.front().value].statement).alternate=retained;
    }
    for (unsigned i = 0; i <= count; ++i) body.push_back(Stmt(ExpressionStmt{Call(2, true)}));
    if (form == 2 || form == 3) {
      const auto branch = form == 2 ? Block(Scope(scope), {Store(5, Call(1, true)), Stmt(ContinueStmt{loop, {}})})
        : Block(Scope(scope), {Stmt(BreakStmt{loop, {}})});
      body.push_back(Stmt(IfStmt{Call(4), branch, {}}));
      if (form == 2) body.push_back(Stmt(TryStmt{Block(Scope(scope), {Stmt(ExpressionStmt{Call(2, true)})}), {},
          Block(Scope(scope), {Stmt(ExpressionStmt{Call(3)})})}));
    }
    auto next = Call(1, true); if (form == 5) next = Expr(AwaitExpr{next}); if (form == 6) next = Expr(YieldExpr{next});
    const auto producer = Store(6, next);
    StmtId mark;
    if (form == 4) {
      // Arbitrary control-dependent expression program in the update clause.
      const auto yes = Block(Scope(scope), {Stmt(ExpressionStmt{Call(3)})});
      const auto no = Block(Scope(scope), {Stmt(ExpressionStmt{Call(3)})});
      mark = Stmt(IfStmt{Call(4), yes, no});
    } else mark = Stmt(ExpressionStmt{Call(3)});
    std::vector<StmtId> update{producer, mark, Store(5, Read(6))};
    if (form == 1) {
      std::vector<ScopeId> scopes; auto parent = scope;
      for (unsigned i = 0; i <= count; ++i) { parent = Scope(parent); scopes.push_back(parent); }
      auto nested = producer;
      for (auto i = scopes.size(); i != 0;) nested = Block(scopes[--i], {nested});
      update[0] = nested;
    }
    if (form == 7) {
      const auto local_scope = Scope(scope);
      tree.symbols.push_back({local_scope, {8}, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kSynthesized, {}});
      update.insert(update.begin(), Stmt(DeclarationStmt{{static_cast<std::uint32_t>(tree.symbols.size() - 1)}, Read(5)}));
      body.push_back(Block(local_scope, update));
    } else if (form == 8) {
      const auto finalizer = Block(Scope(scope), {Stmt(ExpressionStmt{Call(3)})});
      body.push_back(Stmt(TryStmt{Block(Scope(scope), update), {}, finalizer}));
    } else body.insert(body.end(), update.begin(), update.end());
    body.push_back(Stmt(ContinueStmt{loop, {}}));
    std::get<LoopStmt>(tree.statements[loop.value].statement).body = Block(scope, body);
    const auto catch_scope = Scope({1}, ScopeKind::kCatch);
    const SymbolId caught{static_cast<std::uint32_t>(tree.symbols.size())};
    tree.symbols.push_back({catch_scope, {7}, BindingKind::kCatchParameter, DeclarationKind::kCatchParameter, NameEvidence::kSynthesized, {}});
    const auto region = Stmt(TryStmt{Block(protected_scope, {loop}), CatchClause{caught, Block(catch_scope, {Stmt(ReturnStmt{Read(5)})})}, {}});
    const auto initial = Stmt(DeclarationStmt{{5}, Read(0)}), temporary = Stmt(DeclarationStmt{{6}, {}});
    const auto first = static_cast<std::uint32_t>(tree.children.size());
    tree.children.insert(tree.children.end(), {initial, temporary, region, Stmt(ReturnStmt{Read(5)})});
    std::get<BlockStmt>(tree.statements[0].statement) = {{1}, first, 4, {}};
    if (reverse) Reverse();
    for (std::uint32_t s = 0; s < tree.statements.size(); ++s)
      if (const auto* block = std::get_if<BlockStmt>(&tree.statements[s].statement))
        for (std::uint32_t i = 0; i <= block->child_count; ++i) {
          const auto index = static_cast<std::uint32_t>(tree.anchors.size());
          tree.anchors.push_back({BlockBoundary{{s}, i}, {0}});
          tree.operations.push_back({{index}, AnchorId{index}});
        }
  }
  void Reverse() {
    const auto total = static_cast<std::uint32_t>(tree.statements.size()); const auto remap = [&](StmtId id) { return StmtId{total - 1 - id.value}; };
    for (auto& child : tree.children) child = remap(child);
    for (auto& statement : tree.statements) {
      if (auto* node = std::get_if<LoopStmt>(&statement.statement)) node->body = remap(node->body);
      if (auto* node = std::get_if<IfStmt>(&statement.statement)) { node->consequent = remap(node->consequent); if (node->alternate) node->alternate = remap(*node->alternate); }
      if (auto* node = std::get_if<TryStmt>(&statement.statement)) { node->body = remap(node->body); if (node->handler) node->handler->body = remap(node->handler->body); if (node->finalizer) node->finalizer = remap(*node->finalizer); }
      if (auto* node = std::get_if<BreakStmt>(&statement.statement)) node->target = remap(node->target);
      if (auto* node = std::get_if<ContinueStmt>(&statement.statement)) node->target = remap(node->target);
    }
    tree.body = remap(tree.body); loop = remap(loop); std::reverse(tree.statements.begin(), tree.statements.end());
  }
};
std::uint64_t PhaseScopes(unsigned form,unsigned count,bool reverse) {
  Fixture fixture(form,count,reverse);const auto& tree=fixture.tree;
  const auto loops=std::get<LoopSyntax>(PlanLoopSyntax(tree));
  const auto condition=loops.conditions[fixture.loop.value];test::Check(condition.has_value());
  const auto body_id=std::get<LoopStmt>(tree.statements[fixture.loop.value].statement).body;
  const auto& body=std::get<BlockStmt>(tree.statements[body_id.value].statement);
  std::vector<std::uint8_t> headers(tree.statements.size()),increments(tree.statements.size());
  const auto mark=[&](StmtId root,std::optional<StmtId> retained,auto& marked) {
    std::vector<StmtId> pending{root};while(!pending.empty()) {
      const auto id=pending.back();pending.pop_back();if(id==retained)continue;
      marked[id.value]=1;StatementChildren(tree,id,[&](StmtId child){pending.push_back(child);});
    }
  };
  for(unsigned i=0;i<condition->header_count;++i)mark(tree.children[body.first_child+i],condition->body,headers);
  if(const auto& transition=loops.transitions[fixture.loop.value]) {
    const auto& block=std::get<BlockStmt>(tree.statements[transition->block.value].statement);
    for(auto i=transition->first;i<transition->end;++i)mark(tree.children[block.first_child+i],{},increments);
  }
  std::vector<std::optional<SymbolId>> aliases(tree.symbols.size());std::vector<std::optional<PrimitiveId>> literals(tree.symbols.size());
  std::vector<std::optional<ExprId>> replacements(tree.expressions.size());std::vector<std::uint8_t> omitted(tree.statements.size());
  std::vector<ExpressionEvaluation> evaluations(tree.expressions.size(),ExpressionEvaluation::kRequired);std::vector<std::uint8_t> stores(tree.expressions.size(),1);
  LegacyFlowProjection projection{aliases,literals,replacements,omitted,evaluations,stores};projection.loops=&loops;
  const auto flow=std::get<RecoveryFlow>(BuildRecoveryFlow(tree,projection));
  test::Check(flow.loops[fixture.loop.value].has_value());
  const auto stages=*flow.loops[fixture.loop.value];
  test::Check(flow.statement_semantics[tree.body.value]->retention==FlowStatementRetention::kRequired);
  test::Check(flow.statement_semantics[body_id.value]->retention==FlowStatementRetention::kRequired);
  test::Check(flow.scopes->Presence(body.scope,stages.condition,FlowScopePhase::kBefore)==FlowUnitPresence::kInactive);
  test::Check(flow.scopes->Presence(body.scope,stages.increment,FlowScopePhase::kBefore)==FlowUnitPresence::kInactive);
  const auto order=std::get<FlowDominance>(AnalyzeFlowDominance(flow));
  std::optional<FlowPoint> entered;
  for(unsigned i=0;i<flow.events.size();++i)if(flow.events[i].kind==FlowEventKind::kEnterScope&&flow.events[i].scope==body.scope)entered=FlowPoint{i};
  test::Check(entered.has_value());test::Check(!order.Dominates(*entered,stages.condition));
  unsigned tested_headers=0,tested_body=0,tested_increments=0;
  for(const auto& reference:flow.references) {
    if(!reference.owner)continue;const auto owner=reference.owner->value;
    const auto presence=flow.scopes->Presence(body.scope,reference.point,FlowScopePhase::kBefore);
    if(headers[owner]){test::Check(presence==FlowUnitPresence::kInactive);++tested_headers;}
    else if(increments[owner]){test::Check(presence==FlowUnitPresence::kInactive);++tested_increments;}
    else if(presence==FlowUnitPresence::kActive)++tested_body;
  }
  test::Check(tested_headers && tested_body);if(loops.transitions[fixture.loop.value])test::Check(tested_increments);
  unsigned tested_transfers=0;
  for(const auto& edge:flow.edges) {
    if(edge.completion!=FlowCompletion::kJump)continue;
    const auto owner=flow.events[edge.from.value].owner;if(!owner)continue;
    if(const auto* transfer=std::get_if<ContinueStmt>(&tree.statements[owner->value].statement)) {
      test::Check(transfer->target==fixture.loop);
      test::Check(edge.to==(increments[owner->value]?stages.condition:stages.increment));++tested_transfers;
    }
    if(std::holds_alternative<BreakStmt>(tree.statements[owner->value].statement))test::Check(edge.to==stages.exit);
  }
  if(!loops.transitions[fixture.loop.value])test::Check(tested_transfers);
  bool body_exit=false;
  for(const auto& edge:flow.edges)if(flow.events[edge.from.value].kind==FlowEventKind::kLeaveScope&&flow.events[edge.from.value].scope==body.scope) {
    test::Check(edge.to==stages.increment);body_exit=true;
  }
  test::Check(body_exit);
  if(condition->body) {
    test::Check(flow.statement_semantics[condition->body->value]->retention==FlowStatementRetention::kRequired);
    test::Check(flow.statement_semantics[condition->body->value]->parent==body_id);
    test::Check(std::count(flow.statement_order.begin(),flow.statement_order.end(),*condition->body)==1);
  }
  test::Check(std::holds_alternative<RecoveryFlow>(BuildRecoveryFlow(tree,projection,{flow.allocated_bytes,flow.work})));
  test::Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{flow.allocated_bytes-1,flow.work}))==TreeStructureError::kResourceLimit);
  test::Check(std::get<TreeStructureError>(BuildRecoveryFlow(tree,projection,{flow.allocated_bytes,flow.work-1}))==TreeStructureError::kWorkLimit);
  return loops.work+flow.work+order.work;
}

}
int main(int argc, char** argv) {
  for(unsigned form=0;form<12;++form) {
    std::uint64_t previous=0;
    for(unsigned count:{1U,200U,201U,400U,800U})for(bool reverse:{false,true}) {
      const auto work=PhaseScopes(form,count,reverse);
      if(!reverse){if(count>=400)test::Check(work<previous*3);previous=work;}
    }
  }
  if(argc==2 && std::string_view(argv[1])=="--phase-scopes")return 0;
  std::array<std::uint64_t, 2> small{}, large{};
  for (unsigned form = 0; form < 9; ++form) for (unsigned count : {0U, 1U, 200U, 201U, 400U, 800U}) for (bool reverse : {false, true}) {
    Fixture fixture(form, count, reverse);
    const auto planned = PlanLoopSyntax(fixture.tree); test::Check(std::holds_alternative<LoopSyntax>(planned));
    const auto& plan = std::get<LoopSyntax>(planned); const bool native = form != 2 && form != 7 && form != 8;
    test::Check(bool(plan.transitions[fixture.loop.value]) == native);
    if (form < small.size() && !reverse) { if (count == 400) small[form] = plan.work; if (count == 800) large[form] = plan.work; }
    test::Check(std::get<TreeStructureError>(PlanLoopSyntax(fixture.tree, {0})) == TreeStructureError::kResourceLimit);
    std::ostringstream source; const auto printed = PrintFunctionTree(fixture.tree, source);
    test::Check(std::holds_alternative<PrintedFunction>(printed));
    const auto code = source.str();
    const auto& ranges = std::get<PrintedFunction>(printed);
    for (const auto& range : ranges.expressions) test::Check(range.begin < range.end && range.end <= code.size());
    for (const auto& range : ranges.statements) test::Check(range.begin <= range.end && range.end <= code.size());
    for (const auto anchor : ranges.anchors) test::Check(anchor <= code.size());
    if (argc > 1) std::cout << nlohmann::json{{"source", code}, {"form", form}, {"count", count}, {"native", native}} << '\n';
  }
  for (std::size_t i = 0; i < small.size(); ++i) test::Check(large[i] < small[i] * 3);
}
