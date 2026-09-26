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
  tree.symbols[0].name = {0}; shadowed(empty);
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
    names.back() = "SubscriberManager";
    Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kShadowed);
  }
}
}  // namespace
int main() { Contexts(); IndexedBindings(); }
