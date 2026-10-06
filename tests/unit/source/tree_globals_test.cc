#include "../../../src/source/tree_globals.h"

#include <array>
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <string>

namespace {
using namespace abcd::source;
void Check(bool condition) { if (!condition) std::abort(); }
FunctionTree Tree() {
  FunctionTree tree{};
  tree.names = {u"SubscriberManager", u"params"};
  tree.expressions.push_back({GlobalNameExpr{{0}}, {0}});
  tree.symbols.push_back({{0}, {1}, BindingKind::kParameter, DeclarationKind::kParameter,
                          NameEvidence::kMetadata, {}});
  return tree;
}
void Contexts() {
  auto tree = Tree();
  const GlobalScope empty{}; // Test-owned enclosure known to contain no names.
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, nullptr)) == TreeGlobalError::kMissingContext);
  const auto checked = VerifyTreeGlobals(tree, &empty);
  Check(std::holds_alternative<TreeGlobals>(checked));
  const std::array<std::string_view, 2> bindings{"Other", "SubscriberManager"};
  auto shadowed = [&](GlobalScope scope) {
    Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kShadowed);
  };
  GlobalScope scope{};
  scope.enclosing_bindings = bindings; shadowed(scope);
  scope = {}; scope.import_names = bindings; shadowed(scope);
  scope = {}; scope.parameter_bindings = bindings; shadowed(scope);
  scope = {}; scope.class_binding = "SubscriberManager"; shadowed(scope);
  scope = {}; scope.self_binding = "SubscriberManager"; shadowed(scope);
  const HostRealmBinding realm{u"SubscriberManager"};
  scope = {}; scope.realm_binding = &realm; shadowed(scope);
  const HostSymbolBinding symbol{u"SubscriberManager"};
  scope = {}; scope.symbol_binding = &symbol; shadowed(scope);
  tree.symbols[0].name = {0}; shadowed(empty);
  for (const auto name : {u"$arg0", u"$local0", u"ClassValue", u"MethodSelfValue"}) {
    tree = Tree(); tree.names[0] = name;
    Check(std::holds_alternative<TreeGlobals>(VerifyTreeGlobals(tree, &empty)));
    tree.symbols[0].name = {0}; shadowed(empty);
  }
  tree = Tree(); tree.names[0] = u"eval";
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &empty)) == TreeGlobalError::kName);
  tree = Tree();
  std::get<GlobalNameExpr>(tree.expressions[0].expression).name = {99};
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &empty)) == TreeGlobalError::kName);
  tree = Tree();
  abcd::analysis::SemanticBudget budget;
  budget.work = std::get<TreeGlobals>(checked).work;
  Check(std::holds_alternative<TreeGlobals>(VerifyTreeGlobals(tree, &empty, budget)));
  --budget.work;
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &empty, budget)) == TreeGlobalError::kWorkLimit);
  budget = {}; budget.bytes = 0;
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &empty, budget)) == TreeGlobalError::kResourceLimit);
  tree.expressions.clear();
  Check(std::holds_alternative<TreeGlobals>(VerifyTreeGlobals(tree, nullptr)));
}
void IndexedBindings() {
  for (const std::size_t count : {200U, 201U, 4096U}) {
    auto tree = Tree();
    std::vector<std::string> storage;
    storage.reserve(count);
    for (std::size_t i = 0; i < count; ++i) storage.push_back("Binding" + std::to_string(i));
    std::vector<std::string_view> names;
    for (const auto& name : storage) names.push_back(name);
    std::sort(names.begin(), names.end());
    GlobalScope scope{};
    scope.enclosing_bindings = names;
    tree.expressions.resize(count, tree.expressions.front());
    const auto checked = VerifyTreeGlobals(tree, &scope);
    Check(std::holds_alternative<TreeGlobals>(checked));
    // Loose n log n bound including compared characters, not elapsed-time data.
    constexpr std::uint64_t kWorkPerBindingLogBound = 256;
    Check(std::get<TreeGlobals>(checked).work <= kWorkPerBindingLogBound * count * std::bit_width(count));
    auto realm_tree = tree;
    realm_tree.identity.contract = SourceContract::kLanguageAndHostV2;
    for (auto& node : realm_tree.expressions) node.expression = RealmObjectExpr{};
    const auto realm_checked = VerifyTreeGlobals(realm_tree, &scope);
    Check(std::holds_alternative<TreeGlobals>(realm_checked));
    Check(std::get<TreeGlobals>(realm_checked).work <=
        kWorkPerBindingLogBound * count * std::bit_width(count));
    const HostSymbolBinding intrinsic{u"$symbol"}; scope.symbol_binding = &intrinsic;
    for (auto& node : realm_tree.expressions) node.expression = SymbolConstructorExpr{};
    const auto symbol_checked = VerifyTreeGlobals(realm_tree, &scope);
    Check(std::holds_alternative<TreeGlobals>(symbol_checked));
    Check(std::get<TreeGlobals>(symbol_checked).work <=
        kWorkPerBindingLogBound * count * std::bit_width(count));
    names.back() = "SubscriberManager";
    Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kShadowed);
  }
}
void RealmContexts() {
  auto tree = Tree();
  tree.expressions[0].expression = RealmObjectExpr{};
  tree.identity.contract = SourceContract::kLanguageAndHostV2;
  GlobalScope scope{};
  Check(std::holds_alternative<TreeGlobals>(VerifyTreeGlobals(tree, &scope)));
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, nullptr)) == TreeGlobalError::kMissingContext);
  tree.names[1] = u"globalThis";
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kShadowed);
  const HostRealmBinding host{u"$realm"}; scope.realm_binding = &host;
  Check(std::holds_alternative<TreeGlobals>(VerifyTreeGlobals(tree, &scope)));
  tree.identity.contract = SourceContract::kLanguageAndHostV1;
  Check(std::holds_alternative<TreeGlobals>(VerifyTreeGlobals(tree, &scope)));
  scope.realm_binding = nullptr;
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kMissingContext);
  for (const auto name : {u"", u"with space", u"eval", u"arguments"}) {
    const HostRealmBinding invalid{name}; scope.realm_binding = &invalid;
    Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kMissingContext);
  }
  scope.realm_binding = &host;
  const std::array<std::string_view, 1> names{"$realm"};
  scope.enclosing_bindings = names;
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kShadowed);
}
void SymbolContexts() {
  auto tree = Tree(); tree.expressions[0].expression = SymbolConstructorExpr{};
  for (const auto contract : {SourceContract::kLanguageAndHostV1, SourceContract::kLanguageAndHostV2}) {
    tree.identity.contract = contract;
    GlobalScope scope{};
    Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kMissingContext);
    const HostRealmBinding realm{u"$symbol"}; scope.realm_binding = &realm;
    Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kMissingContext);
    const HostSymbolBinding host{u"$symbol"}; scope.symbol_binding = &host;
    Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kShadowed);
    scope.realm_binding = nullptr;
    Check(std::holds_alternative<TreeGlobals>(VerifyTreeGlobals(tree, &scope)));
    const auto valid = std::get<TreeGlobals>(VerifyTreeGlobals(tree, &scope));
    abcd::analysis::SemanticBudget budget; budget.work = valid.work;
    Check(std::holds_alternative<TreeGlobals>(VerifyTreeGlobals(tree, &scope, budget)));
    --budget.work;
    Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope, budget)) == TreeGlobalError::kWorkLimit);
    tree.names[1] = u"$symbol";
    Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kShadowed);
    tree.names[1] = u"params";
    const std::array<std::string_view, 1> names{"$symbol"};
    for (const auto field : {&GlobalScope::enclosing_bindings, &GlobalScope::import_names,
        &GlobalScope::parameter_bindings}) {
      auto shadowed = scope; shadowed.*field = names;
      Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &shadowed)) == TreeGlobalError::kShadowed);
    }
    scope = {};
    for (const auto name : {u"", u"with space", u"eval", u"arguments"}) {
      const HostSymbolBinding invalid{name}; scope.symbol_binding = &invalid;
      Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kMissingContext);
    }
  }
}
void SnapshotContexts() {
  auto tree = Tree(); tree.expressions[0].expression = ArgumentSnapshotExpr{{0}, {0, 0}};
  GlobalScope scope{};
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kMissingContext);
  const HostArgumentSnapshotBinding host{u"$snapshot", ArgumentSnapshotAbi::kV7CurrentSlots};
  scope.argument_snapshot_binding = &host;
  Check(std::holds_alternative<TreeGlobals>(VerifyTreeGlobals(tree, &scope)));
  const HostSymbolBinding collision{host.name}; scope.symbol_binding = &collision;
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kShadowed);
  scope.symbol_binding = nullptr;
  tree.names[1] = host.name;
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kShadowed);
  tree.names[1] = u"params";
  const std::array<std::string_view, 1> names{"$snapshot"};
  for (const auto field : {&GlobalScope::enclosing_bindings, &GlobalScope::import_names,
      &GlobalScope::parameter_bindings}) {
    auto shadowed = scope; shadowed.*field = names;
    Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &shadowed)) == TreeGlobalError::kShadowed);
  }
  for (const auto name : {u"", u"arguments", u"eval", u"with space"}) {
    const HostArgumentSnapshotBinding invalid{name, ArgumentSnapshotAbi::kV7CurrentSlots};
    scope.argument_snapshot_binding = &invalid;
    Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kMissingContext);
  }
}
}  // namespace
int main() { Contexts(); IndexedBindings(); RealmContexts(); SymbolContexts(); SnapshotContexts(); }
