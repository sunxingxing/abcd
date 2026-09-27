#include "../../../src/source/family_enclosure.h"

#include <cstdlib>

namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool condition) { if (!condition) std::abort(); }
FunctionTree Tree(std::uint32_t owner) {
  FunctionTree tree{};
  tree.owner = {owner}; tree.callable = CallableKind::kFunction;
  tree.strictness = Strictness::kStrict;
  tree.parameter_scope = {0}; tree.body_scope = {1}; tree.body = {0};
  tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
  tree.origins.emplace_back(OriginalSite{{0}, {0}});
  tree.expressions.push_back({TreeLiteral{UndefinedLiteral{}}, {0}});
  tree.statements = {{BlockStmt{{1}, 0, 1}, {0}}, {ReturnStmt{ExprId{0}}, {0}}};
  tree.children = {{1}};
  return tree;
}
ConstructedFamily Chain(std::uint32_t count) {
  // Scope-only fixtures, not executable/capture evidence. The separate family
  // and capture suites cover IR ownership; this check consumes lexical layout.
  ConstructedFamily family; family.root = {0};
  for (std::uint32_t i = 0; i < count; ++i) {
    auto tree = Tree(i);
    if (i + 1 < count) tree.expressions[0].expression = FunctionExpr{{i + 1}, {0, 0}};
    else { tree.names = {u"target"}; tree.expressions[0].expression = GlobalNameExpr{{0}}; }
    family.members.push_back({std::move(tree), {}, {}});
    family.parents.push_back(i ? std::optional{FamilyParent{{i - 1}, {0}}} : std::nullopt);
    family.order.push_back({i});
  }
  return family;
}
void Local(FunctionTree& tree, ScopeId scope) {
  const auto name = NameId{static_cast<std::uint32_t>(tree.names.size())};
  tree.names.emplace_back(u"target");
  tree.symbols.push_back({scope, name, BindingKind::kLocal, DeclarationKind::kLet, NameEvidence::kDebugRange, {}});
  tree.statements.push_back({DeclarationStmt{{0}, {}}, {0}});
}
void Expect(const ConstructedFamily& family, FamilyEnclosureError error) {
  const auto checked = VerifyFamilyEnclosure(family);
  Check(std::holds_alternative<FamilyEnclosureFailure>(checked));
  Check(std::get<FamilyEnclosureFailure>(checked).kind == error);
}
void ScopeCases() {
  auto own = Chain(1);
  Local(own.members[0].tree, {1});
  own.members[0].tree.statements[0].statement = BlockStmt{{1}, 0, 2};
  own.members[0].tree.children = {{2}, {1}};
  Expect(own, FamilyEnclosureError::kShadowedGlobal);
  auto family = Chain(2);
  Check(std::get<FamilyEnclosure>(VerifyFamilyEnclosure(family)).global_reads == 1);
  auto& root = family.members[0].tree;
  Local(root, {1});
  root.statements[0].statement = BlockStmt{{1}, 0, 2}; root.children = {{2}, {1}};
  const auto failure = std::get<FamilyEnclosureFailure>(VerifyFamilyEnclosure(family));
  Check(failure.kind == FamilyEnclosureError::kShadowedGlobal && failure.member == FamilyId{1} && failure.name == NameId{0});
  // TDZ bindings count before their declaration executes too.
  root.children = {{1}, {2}};
  Expect(family, FamilyEnclosureError::kShadowedGlobal);
  // A sibling block is not an enclosing scope of the actual creation expression.
  root.scopes.push_back({ScopeKind::kBlock, ScopeId{1}});
  root.symbols[0].scope = {2};
  root.statements.push_back({BlockStmt{{2}, 2, 1}, {0}});
  root.children = {{3}, {1}, {2}};
  Check(std::get<FamilyEnclosure>(VerifyFamilyEnclosure(family)).global_reads == 1);
  // Moving the creation into that block changes enclosure, not merely owner.
  root.statements[0].statement = BlockStmt{{1}, 0, 1};
  root.statements[3].statement = BlockStmt{{2}, 1, 2}; root.children = {{3}, {2}, {1}};
  Expect(family, FamilyEnclosureError::kShadowedGlobal);
  family = Chain(2);
  // Runtime function names do not introduce a lexical self binding.
  family.members[0].tree.names = {u"target"}; family.members[0].tree.inferred_name = NameId{0};
  Check(std::holds_alternative<FamilyEnclosure>(VerifyFamilyEnclosure(family)));
  family.parents[1]->expression = {99}; Expect(family, FamilyEnclosureError::kLayout);
  family = Chain(2); family.parents[0] = FamilyParent{{1}, {0}};
  Expect(family, FamilyEnclosureError::kLayout);
}
void Bounds() {
  std::uint64_t previous = 0;
  for (const auto count : {200U, 201U, 402U, 804U}) {
    auto family = Chain(count);
    for (auto& member : family.members) {
      Local(member.tree, {1}); member.tree.names.back() = u"other";
      member.tree.statements[0].statement = BlockStmt{{1}, 0, 2};
      member.tree.children = {{2}, {1}};
    }
    const auto checked = std::get<FamilyEnclosure>(VerifyFamilyEnclosure(family));
    Check(checked.global_reads == 1);
    if (previous) Check(checked.work < previous * 3);
    previous = checked.work;
    analysis::SemanticBudget budget; budget.work = checked.work;
    Check(std::holds_alternative<FamilyEnclosure>(VerifyFamilyEnclosure(family, budget)));
    --budget.work;
    Check(std::get<FamilyEnclosureFailure>(VerifyFamilyEnclosure(family, budget)).kind == FamilyEnclosureError::kWorkLimit);
    budget = {}; budget.bytes = 0;
    Check(std::get<FamilyEnclosureFailure>(VerifyFamilyEnclosure(family, budget)).kind == FamilyEnclosureError::kResourceLimit);
    family.members[0].tree.names.back() = u"target";
    Expect(family, FamilyEnclosureError::kShadowedGlobal);
  }
}
void DestinationCases() {
  for (const TreeExpression expression : {TreeExpression{RealmObjectExpr{}}, TreeExpression{SymbolConstructorExpr{}}}) {
    auto realm_family = Chain(2);
    realm_family.members.back().tree.expressions[0].expression = expression;
    const bool realm = std::holds_alternative<RealmObjectExpr>(expression);
    const HostRealmBinding host{u"target"};
    const HostSymbolBinding symbol{u"target"};
    GlobalScope scope{};
    scope.realm_binding = realm ? &host : nullptr; scope.symbol_binding = realm ? nullptr : &symbol;
    const auto count = [&](const FamilyEnclosure& result) { return realm ? result.realm_reads : result.symbol_reads; };
    Check(count(std::get<FamilyEnclosure>(VerifyFamilyDestination(realm_family, &scope))) == 1);
    scope.realm_binding = &host; scope.symbol_binding = &symbol;
    Check(std::get<FamilyEnclosureFailure>(VerifyFamilyDestination(realm_family, &scope)).kind ==
        FamilyEnclosureError::kShadowedGlobal);
    scope.realm_binding = realm ? &host : nullptr; scope.symbol_binding = realm ? nullptr : &symbol;
    auto& root = realm_family.members[0].tree;
    Local(root, {1});
    root.statements[0].statement = BlockStmt{{1}, 0, 2}; root.children = {{2}, {1}};
    Check(std::get<FamilyEnclosureFailure>(VerifyFamilyDestination(realm_family, &scope)).kind ==
        FamilyEnclosureError::kShadowedGlobal);
    // A sibling's binding is not an ancestor of the child creation occurrence.
    root.scopes.push_back({ScopeKind::kBlock, ScopeId{1}}); root.symbols[0].scope = {2};
    root.statements.push_back({BlockStmt{{2}, 2, 1}, {0}}); root.children = {{3}, {1}, {2}};
    Check(count(std::get<FamilyEnclosure>(VerifyFamilyDestination(realm_family, &scope))) == 1);
  }
  auto family = Chain(2);
  const auto missing = std::get<FamilyEnclosureFailure>(VerifyFamilyDestination(family, nullptr));
  Check(missing.kind == FamilyEnclosureError::kMissingContext &&
      missing.member == FamilyId{1} && missing.name == NameId{0});
  const GlobalScope empty{};
  Check(std::get<FamilyEnclosure>(VerifyFamilyDestination(family, &empty)).global_reads == 1);
  const std::string_view names[]{"before", "target", "zebra"};
  GlobalScope scope{};
  const auto shadowed = [&] {
    const auto failure = std::get<FamilyEnclosureFailure>(VerifyFamilyDestination(family, &scope));
    Check(failure.kind == FamilyEnclosureError::kShadowedGlobal && failure.member == FamilyId{1});
  };
  scope.enclosing_bindings = names; shadowed(); scope = {};
  scope.import_names = names; shadowed(); scope = {};
  scope.parameter_bindings = names; shadowed(); scope = {};
  scope.class_binding = "target"; shadowed(); scope = {};
  scope.self_binding = "target"; shadowed(); scope = {};
  const HostRealmBinding realm{u"target"}; scope.realm_binding = &realm; shadowed();
  // Success under one destination is not permission to emit under another.
  Check(std::holds_alternative<FamilyEnclosure>(VerifyFamilyDestination(family, &empty)));
  shadowed();
  family.members.back().tree.names[0] = u"eval";
  Check(std::get<FamilyEnclosureFailure>(VerifyFamilyDestination(family, &empty)).kind == FamilyEnclosureError::kStructure);
  family.members.back().tree = Tree(1);
  Check(std::get<FamilyEnclosure>(VerifyFamilyDestination(family, nullptr)).global_reads == 0);
  // Declaration in a sibling block must not become a function-wide collision.
  family = Chain(2);
  auto& root = family.members[0].tree;
  Local(root, {2}); root.scopes.push_back({ScopeKind::kBlock, ScopeId{1}});
  root.statements[0].statement = BlockStmt{{1}, 0, 2};
  root.statements.push_back({BlockStmt{{2}, 2, 1}, {0}});
  root.children = {{3}, {1}, {2}};
  const auto checked = std::get<FamilyEnclosure>(VerifyFamilyDestination(family, &empty));
  analysis::SemanticBudget budget; budget.work = checked.work;
  Check(std::holds_alternative<FamilyEnclosure>(VerifyFamilyDestination(family, &empty, budget)));
  --budget.work;
  Check(std::get<FamilyEnclosureFailure>(VerifyFamilyDestination(family, &empty, budget)).kind == FamilyEnclosureError::kWorkLimit);
}
void ModuleAlias(FunctionTree& tree, bool read) {
  const auto scope = ScopeId{static_cast<std::uint32_t>(tree.scopes.size())};
  const auto name = NameId{static_cast<std::uint32_t>(tree.names.size())};
  const auto symbol = SymbolId{static_cast<std::uint32_t>(tree.symbols.size())};
  tree.scopes.push_back({ScopeKind::kModule, {}});
  tree.module_scope = scope; tree.scopes[tree.parameter_scope.value].parent = scope;
  tree.names.emplace_back(u"target");
  tree.symbols.push_back({scope, name, BindingKind::kModule, DeclarationKind::kConst, NameEvidence::kMetadata, {}});
  tree.modules.push_back({symbol, {{0}, {0}}, ModuleAccess::kReadOnly});
  if (read) tree.expressions[0].expression = SymbolExpr{symbol};
}
void ModuleScopes() {
  auto family = Chain(2);
  ModuleAlias(family.members[1].tree, true);
  auto checked = std::get<FamilyEnclosure>(VerifyFamilyEnclosure(family));
  Check(checked.module_reads == 1 && checked.global_reads == 0);
  const GlobalScope empty{};
  // A lexical check is not a record binding or TDZ execution certificate.
  Check(std::get<FamilyEnclosureFailure>(VerifyFamilyDestination(family, &empty)).kind ==
      FamilyEnclosureError::kMissingModuleContext);
  auto& root = family.members[0].tree;
  Local(root, {1});
  root.statements[0].statement = BlockStmt{{1}, 0, 2}; root.children = {{2}, {1}};
  Expect(family, FamilyEnclosureError::kShadowedModule);
  root.children = {{1}, {2}};
  Expect(family, FamilyEnclosureError::kShadowedModule); // Parent TDZ before declaration.
  root.scopes.push_back({ScopeKind::kBlock, ScopeId{1}}); root.symbols[0].scope = {2};
  root.statements.push_back({BlockStmt{{2}, 2, 1}, {0}}); root.children = {{3}, {1}, {2}};
  Check(std::get<FamilyEnclosure>(VerifyFamilyEnclosure(family)).module_reads == 1);
  root.statements[0].statement = BlockStmt{{1}, 0, 1};
  root.statements[3].statement = BlockStmt{{2}, 1, 2}; root.children = {{3}, {2}, {1}};
  Expect(family, FamilyEnclosureError::kShadowedModule);
  family = Chain(3);
  for (std::size_t i = 0; i < family.members.size(); ++i)
    ModuleAlias(family.members[i].tree, i + 1 == family.members.size());
  Check(std::get<FamilyEnclosure>(VerifyFamilyEnclosure(family)).module_reads == 1);
  auto changed = family;
  changed.members[0].tree.modules[0].target.symbol = {1};
  Expect(changed, FamilyEnclosureError::kModuleBinding); // Same name, different slot.
  changed = family;
  changed.members[0].tree.names.back() = u"other";
  Expect(changed, FamilyEnclosureError::kModuleBinding); // Same slot, different name.
  std::uint64_t previous = 0;
  for (const auto count : {200U, 201U, 402U, 804U}) {
    auto nested = Chain(count);
    for (std::size_t i = 0; i < nested.members.size(); ++i)
      ModuleAlias(nested.members[i].tree, i + 1 == nested.members.size());
    checked = std::get<FamilyEnclosure>(VerifyFamilyEnclosure(nested));
    Check(checked.module_reads == 1 && (!previous || checked.work < previous * 3));
    previous = checked.work;
    analysis::SemanticBudget budget; budget.work = checked.work;
    Check(std::holds_alternative<FamilyEnclosure>(VerifyFamilyEnclosure(nested, budget)));
    --budget.work;
    Check(std::get<FamilyEnclosureFailure>(VerifyFamilyEnclosure(nested, budget)).kind == FamilyEnclosureError::kWorkLimit);
  }
}
}  // namespace
int main() { ScopeCases(); Bounds(); DestinationCases(); ModuleScopes(); }
