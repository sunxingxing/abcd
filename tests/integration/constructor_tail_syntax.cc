#include "constructor_tail_syntax.h"
#include "../../src/source/class_tree_context.h"
#include "../../src/source/family_print.h"
#include "../../src/source/instruction_sources.h"
#include "../../tools/tree_position_json.h"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>
using namespace abcd;
using namespace abcd::source;
namespace {
enum class TailCase { kDirect, kEvent, kMissingSuper, kValue, kCall, kSequence,
    kEarly, kNested, kCatchBeforeSuper, kFinally, kAlias, kMember };
std::string_view Label(TailCase kind) {
  switch (kind) {
    case TailCase::kDirect: return "direct";
    case TailCase::kEvent: return "event";
    case TailCase::kMissingSuper: return "missing-super";
    case TailCase::kValue: return "value";
    case TailCase::kCall: return "call";
    case TailCase::kSequence: return "sequence";
    case TailCase::kEarly: return "early";
    case TailCase::kNested: return "nested";
    case TailCase::kCatchBeforeSuper: return "catch-before-super";
    case TailCase::kFinally: return "finally";
    case TailCase::kAlias: return "alias";
    case TailCase::kMember: return "member";
  }
  std::abort();
}
struct Syntax {
  FunctionTree tree{};
  explicit Syntax(const ClassTreeContext& context) {
    tree.identity = context.Identity(); tree.method = context.Target();
    tree.owner = {0}; tree.callable = context.Callable(); tree.strictness = Strictness::kStrict;
    tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
    tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
    tree.origins = {OriginalSite{{0}, {0}}};
    tree.names = {u"event", u"value", u"receiver", u"error"};
    tree.symbols = {{{0}, {0}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}},
        {{0}, {1}, BindingKind::kParameter, DeclarationKind::kParameter, NameEvidence::kSynthesized, {}}};
    tree.parameters = {{{0}, {}, false}, {{1}, {}, false}};
    tree.statements = {{BlockStmt{{1}, 0, 0}, {0}}};
  }
  ExprId Expr(TreeExpression expression) {
    const ExprId id{static_cast<std::uint32_t>(tree.expressions.size())};
    tree.expressions.push_back({std::move(expression), {0}}); return id;
  }
  StmtId Stmt(TreeStatement statement) {
    const StmtId id{static_cast<std::uint32_t>(tree.statements.size())};
    tree.statements.push_back({std::move(statement), {0}}); return id;
  }
  ExprId Receiver() { return Expr(ThisExpr{tree.owner}); }
  ExprId Value() { return Expr(SymbolExpr{{1}}); }
  ExprId Event() { return Expr(CallExpr{Expr(SymbolExpr{{0}}), {0, 0}, CallReference::kValue}); }
  ScopeId Scope(ScopeKind kind) {
    const ScopeId id{static_cast<std::uint32_t>(tree.scopes.size())};
    tree.scopes.push_back({kind, tree.body_scope}); return id;
  }
  BlockStmt Block(ScopeId scope, const std::vector<StmtId>& children) {
    const auto first = static_cast<std::uint32_t>(tree.children.size());
    tree.children.insert(tree.children.end(), children.begin(), children.end());
    return {scope, first, static_cast<std::uint32_t>(children.size())};
  }
};
}
void ConstructorTailSyntax(const ClassTreeContext& context) {
  // These are native syntax/effect/exception oracles, not replacement decoded
  // bodies or ABC publication evidence. Paired constructor tests exercise the
  // actual engine separately; this fixture reuses its real class destination.
  const bool derived = context.Callable() == CallableKind::kDerivedConstructor;
  for (const auto kind : {TailCase::kDirect, TailCase::kEvent, TailCase::kMissingSuper,
      TailCase::kValue, TailCase::kCall, TailCase::kSequence, TailCase::kEarly,
      TailCase::kNested, TailCase::kCatchBeforeSuper, TailCase::kFinally, TailCase::kAlias, TailCase::kMember}) {
    Syntax syntax(context); auto& tree = syntax.tree;
    std::vector<StmtId> children;
    if (derived && kind != TailCase::kMissingSuper && kind != TailCase::kCatchBeforeSuper) {
      tree.arguments.push_back(syntax.Value());
      children.push_back(syntax.Stmt(ExpressionStmt{syntax.Expr(SuperCallExpr{{0, 1}})}));
    }
    if (kind == TailCase::kEvent) children.push_back(syntax.Stmt(ExpressionStmt{syntax.Event()}));
    ExprId result;
    if (kind == TailCase::kValue) result = syntax.Value();
    else if (kind == TailCase::kCall) result = syntax.Event();
    else if (kind == TailCase::kSequence) result = syntax.Expr(SequenceExpr{syntax.Event(), syntax.Receiver()});
    else if (kind == TailCase::kMember) result = syntax.Expr(MemberExpr{syntax.Receiver(), NameId{1}});
    else if (kind == TailCase::kAlias) {
      tree.symbols.push_back({{1}, {2}, BindingKind::kLocal, DeclarationKind::kConst, NameEvidence::kSynthesized, {}});
      children.push_back(syntax.Stmt(DeclarationStmt{{2}, syntax.Receiver()}));
      result = syntax.Expr(SymbolExpr{{2}});
    } else result = syntax.Receiver();
    const auto returned = syntax.Stmt(ReturnStmt{result});
    if (kind == TailCase::kNested) {
      children.push_back(syntax.Stmt(syntax.Block(syntax.Scope(ScopeKind::kBlock), {returned})));
    } else if (kind == TailCase::kCatchBeforeSuper) {
      const auto body = syntax.Stmt(syntax.Block(syntax.Scope(ScopeKind::kBlock), {returned}));
      const auto scope = syntax.Scope(ScopeKind::kCatch);
      tree.symbols.push_back({scope, {3}, BindingKind::kCatchParameter, DeclarationKind::kCatchParameter, NameEvidence::kSynthesized, {}});
      const auto caught = syntax.Stmt(syntax.Block(scope, {
          syntax.Stmt(ExpressionStmt{syntax.Event()}), syntax.Stmt(ReturnStmt{syntax.Value()})}));
      children.push_back(syntax.Stmt(TryStmt{body, CatchClause{{2}, caught}, {}}));
    } else if (kind == TailCase::kFinally) {
      const auto body = syntax.Stmt(syntax.Block(syntax.Scope(ScopeKind::kBlock), {returned}));
      const auto finalizer = syntax.Stmt(syntax.Block(syntax.Scope(ScopeKind::kBlock), {syntax.Stmt(ExpressionStmt{syntax.Event()})}));
      children.push_back(syntax.Stmt(TryStmt{body, {}, finalizer}));
    } else children.push_back(returned);
    if (kind == TailCase::kEarly || kind == TailCase::kNested)
      children.push_back(syntax.Stmt(ExpressionStmt{syntax.Event()}));
    tree.statements[0].statement = syntax.Block(tree.body_scope, children);
    ConstructedFamily family; family.root = {0}; family.members = {{tree, {}, {}}};
    std::ostringstream source;
    const auto printed = PrintClassFamilyDraft(family, context, source);
    if (const auto* error = std::get_if<TreePrintError>(&printed)) {
      std::cerr << Label(kind) << " print error " << unsigned(*error) << '\n'; std::abort();
    }
    const auto& positions = std::get<PrintedFamilyDraft>(printed).members[0];
    InstructionSources sources; sources.input_sites = 1;
    sources.occurrences = {{{0}, {0}, result}, {{0}, {0}, returned}};
    std::ostringstream mapping; tools::WriteTreePositions(mapping, tree, positions, &sources);
    const bool omitted = kind == TailCase::kDirect || kind == TailCase::kEvent || kind == TailCase::kMissingSuper;
    std::cout << nlohmann::json{{"derived", derived}, {"case", Label(kind)}, {"omitted", omitted},
        {"source", source.str()}, {"positions", nlohmann::json::parse(mapping.str())},
        {"return", returned.value}, {"operand", result.value}} << '\n';
  }
}
