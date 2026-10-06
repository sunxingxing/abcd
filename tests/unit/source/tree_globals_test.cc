#include "../../../src/source/tree_globals.h"
#include "../../../src/source/tree_structure.h"
#include "abcd/source/tree_printer.h"

#include <array>
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <string>
#include <iostream>
#include <source_location>
#include <sstream>

namespace {
using namespace abcd::source;
void Check(bool condition, const std::source_location& at = std::source_location::current()) {
  if (!condition) { std::cerr << "global scope check " << at.line() << '\n'; std::abort(); }
}
FunctionTree Tree() {
  FunctionTree tree{};
  tree.callable = CallableKind::kFunction;
  tree.strictness = Strictness::kStrict;
  tree.parameter_scope = {0}; tree.body_scope = {1};
  tree.scopes = {{ScopeKind::kParameters, {}}, {ScopeKind::kFunctionBody, ScopeId{0}}};
  tree.origins.emplace_back(OriginalSite{{0}, {0}});
  tree.names = {u"SubscriberManager", u"params"};
  tree.expressions.push_back({GlobalNameExpr{{0}}, {0}});
  tree.symbols.push_back({{0}, {1}, BindingKind::kParameter, DeclarationKind::kParameter,
                          NameEvidence::kMetadata, {}});
  tree.parameters.push_back({{0}, {}});
  tree.statements = {{BlockStmt{{1}, 0, 1}, {0}}, {ReturnStmt{ExprId{0}}, {0}}};
  tree.children = {{1}};
  return tree;
}
void Wide(FunctionTree& tree, std::size_t count) {
  tree.expressions.resize(count, tree.expressions.front());
  tree.statements.resize(1); tree.children.clear();
  for (std::uint32_t i = 0; i < count; ++i) {
    tree.statements.push_back({ExpressionStmt{{i}}, {0}}); tree.children.push_back({i + 1});
  }
  tree.statements[0].statement = BlockStmt{{1}, 0, static_cast<std::uint32_t>(count)};
}
void LocalScopes() {
  const GlobalScope destination{};
  auto tree = Tree();
  tree.scopes.push_back({ScopeKind::kBlock, ScopeId{1}});
  tree.symbols.push_back({{2}, {0}, BindingKind::kLocal, DeclarationKind::kLet,
      NameEvidence::kDebugRange, {}});
  tree.statements = {{BlockStmt{{1}, 0, 2}, {0}}, {BlockStmt{{2}, 2, 1}, {0}},
      {DeclarationStmt{{1}, {}}, {0}}, {ReturnStmt{ExprId{0}}, {0}}};
  tree.children = {{1}, {3}, {2}};
  // The declaration is inside a completed sibling block. It cannot shadow
  // the actual global read in the enclosing function body.
  Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
  Check(std::holds_alternative<TreeGlobals>(VerifyTreeGlobals(tree, &destination)));
  // Reverse sibling order; lexical lookup does not depend on traversal order.
  std::swap(tree.children[0], tree.children[1]);
  Check(std::holds_alternative<TreeGlobals>(VerifyTreeGlobals(tree, &destination)));
  // Put the read in that block before its declaration: TDZ still shadows.
  tree.statements[0].statement = BlockStmt{{1}, 0, 1};
  tree.statements[1].statement = BlockStmt{{2}, 1, 2};
  tree.children = {{1}, {3}, {2}};
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &destination)) == TreeGlobalError::kShadowed);
  std::swap(tree.children[1], tree.children[2]);
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &destination)) == TreeGlobalError::kShadowed);
}
enum class FreeRead { kGlobal, kRealm, kSymbol, kSnapshot };
FunctionTree ScopedTree(FreeRead read, std::size_t count, bool read_first) {
  auto tree = Tree();
  tree.names[0] = read == FreeRead::kGlobal ? u"SubscriberManager" :
      read == FreeRead::kRealm ? u"$realm" : read == FreeRead::kSymbol ? u"$symbol" : u"$snapshot";
  switch (read) {
    case FreeRead::kGlobal: break;
    case FreeRead::kRealm: tree.expressions[0].expression = RealmObjectExpr{}; break;
    case FreeRead::kSymbol: tree.expressions[0].expression = SymbolConstructorExpr{}; break;
    case FreeRead::kSnapshot:
      tree.expressions[0].expression = ArgumentSnapshotExpr{{0}, {0, 1}};
      tree.expressions.push_back({SymbolExpr{{0}}, {0}}); tree.arguments.push_back({1}); break;
  }
  tree.statements.resize(1); tree.children.clear();
  std::vector<StmtId> declarations;
  for (std::uint32_t i = 0; i < count; ++i) {
    const ScopeId scope{static_cast<std::uint32_t>(tree.scopes.size())};
    tree.scopes.push_back({ScopeKind::kBlock, tree.body_scope});
    const SymbolId symbol{static_cast<std::uint32_t>(tree.symbols.size())};
    tree.symbols.push_back({scope, {0}, BindingKind::kLocal, DeclarationKind::kLet,
        NameEvidence::kDebugRange, {}});
    const StmtId block{static_cast<std::uint32_t>(tree.statements.size())};
    tree.statements.push_back({BlockStmt{scope, static_cast<std::uint32_t>(count + 1 + i), 1}, {0}});
    declarations.push_back({block.value + 1});
    tree.statements.push_back({DeclarationStmt{symbol, {}}, {0}}); tree.children.push_back(block);
  }
  const StmtId result{static_cast<std::uint32_t>(tree.statements.size())};
  tree.statements.push_back({ReturnStmt{ExprId{0}}, {0}});
  tree.children.insert(read_first ? tree.children.begin() : tree.children.end(), result);
  tree.children.insert(tree.children.end(), declarations.begin(), declarations.end());
  tree.statements[0].statement = BlockStmt{{1}, 0, static_cast<std::uint32_t>(count + 1)};
  return tree;
}
GlobalScope Capabilities() {
  static const HostRealmBinding realm{u"$realm"};
  static const HostSymbolBinding symbol{u"$symbol"};
  static const HostArgumentSnapshotBinding snapshot{u"$snapshot", ArgumentSnapshotAbi::kV7CurrentSlots};
  GlobalScope scope{}; scope.realm_binding = &realm; scope.symbol_binding = &symbol;
  scope.argument_snapshot_binding = &snapshot; return scope;
}
void ScopedReads(bool print) {
  const auto scope = Capabilities();
  if (print) std::cout << '[';
  bool first = true;
  for (const auto read : {FreeRead::kGlobal, FreeRead::kRealm, FreeRead::kSymbol, FreeRead::kSnapshot}) {
    std::uint64_t previous = 0;
    for (const std::size_t count : {0U, 1U, 200U, 201U, 402U, 804U}) {
      for (const bool read_first : {false, true}) {
        auto tree = ScopedTree(read, count, read_first);
        const auto checked = VerifyTreeGlobals(tree, &scope);
        Check(std::holds_alternative<TreeGlobals>(checked));
        if (!read_first) {
          const auto work = std::get<TreeGlobals>(checked).work;
          if (count > 201) Check(work < 3 * previous);
          previous = work;
        }
        std::ostringstream output;
        const auto printed = PrintFunctionTree(tree, output, {}, {}, &scope);
        Check(std::holds_alternative<PrintedFunction>(printed));
        const auto& ranges = std::get<PrintedFunction>(printed);
        Check(ranges.expressions[0].begin < ranges.expressions[0].end);
        Check(ranges.statements.back().begin < ranges.statements.back().end);
        if (print) {
          if (!first) std::cout << ','; first = false;
          // Test-only JS array boundary. Production owns source syntax.
          std::cout << output.str();
        }
        // An actual body binding shadows every read, including before init.
        const SymbolId shadow{static_cast<std::uint32_t>(tree.symbols.size())};
        tree.symbols.push_back({{1}, {0}, BindingKind::kLocal, DeclarationKind::kLet,
            NameEvidence::kDebugRange, {}});
        const StmtId declaration{static_cast<std::uint32_t>(tree.statements.size())};
        tree.statements.push_back({DeclarationStmt{shadow, {}}, {0}});
        for (auto& node : tree.statements) {
          if (auto* block = std::get_if<BlockStmt>(&node.statement); block && block->scope != tree.body_scope)
            ++block->first_child;
        }
        tree.children.insert(tree.children.begin() + count + 1, declaration);
        ++std::get<BlockStmt>(tree.statements[0].statement).child_count;
        Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(tree)));
        Check(std::get<TreeGlobalError>(VerifyTreeGlobals(tree, &scope)) == TreeGlobalError::kShadowed);
      }
    }
  }
  if (print) std::cout << "]\n";
  auto malformed = Tree(); malformed.expressions.push_back(malformed.expressions[0]);
  Check(std::get<TreeGlobalError>(VerifyTreeGlobals(malformed, &scope)) == TreeGlobalError::kStructure);
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
    Wide(tree, count);
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
  auto tree = Tree(); tree.expressions[0].expression = ArgumentSnapshotExpr{{0}, {0, 1}};
  tree.expressions.push_back({SymbolExpr{{0}}, {0}}); tree.arguments.push_back({1});
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
int main(int argc, char** argv) {
  if (argc == 2) { Check(std::string_view{argv[1]} == "--scope-source"); ScopedReads(true); return 0; }
  LocalScopes(); ScopedReads(false); Contexts(); IndexedBindings(); RealmContexts(); SymbolContexts(); SnapshotContexts();
}
