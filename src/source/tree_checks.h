#ifndef ABCD_SOURCE_TREE_CHECKS_H_
#define ABCD_SOURCE_TREE_CHECKS_H_

#include "tree_structure.h"

namespace abcd::source {
inline void RequireTree(bool condition, TreeStructureError error) {
  if (!condition) throw error;
}
class TreeCheckBudget {
 public:
  explicit TreeCheckBudget(analysis::SemanticBudget budget) : initial_(budget), remaining_(budget) {}
  void Step(std::uint64_t count = 1) {
    RequireTree(count <= remaining_.work, TreeStructureError::kWorkLimit);
    remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    RequireTree(count <= remaining_.bytes / width, TreeStructureError::kResourceLimit);
    remaining_.bytes -= count * width;
  }
  template <class T> void Array(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T));
    Step(count);
    values.resize(count);
    Admit(values.capacity() - count, sizeof(T));
  }
  [[nodiscard]] std::size_t Bytes() const { return initial_.bytes - remaining_.bytes; }
  [[nodiscard]] std::uint64_t Work() const { return initial_.work - remaining_.work; }
  [[nodiscard]] analysis::SemanticBudget Remaining() const { return remaining_; }
 private:
  analysis::SemanticBudget initial_;
  analysis::SemanticBudget remaining_;
};

// Scope activation uses one slot per distinct spelling. SymbolId references
// resolve in O(1), free spellings in O(log B), without per-use parent walks.
class TreeScopeCheck {
 public:
  TreeScopeCheck(const FunctionTree& tree, TreeCheckBudget& budget,
      std::span<const SourceBindingPresence> presence = {});
  void Enter(ScopeId scope);
  void Leave(ScopeId scope);
  void Reference(SymbolId symbol, bool assignment = false);
  [[nodiscard]] bool Resolves(SymbolId symbol);
  // A printing preference only. Duplicate spellings remain valid bindings and
  // still use actual occurrence resolution; uniqueness never proves lifetime.
  [[nodiscard]] bool UniqueSpelling(SymbolId symbol);
  [[nodiscard]] SourceSpellingId BindingSpelling(SymbolId symbol);
  [[nodiscard]] std::optional<SourceSpellingId> NameSpelling(NameId name);
  // Query while the binding's own scope is active; identity, not name text.
  [[nodiscard]] std::optional<SymbolId> ShadowedBinding(SymbolId symbol);
  void EnclosingReference(std::u16string_view name, const ModuleSymbol* module = nullptr);
  void FreeReference(std::u16string_view name);
  [[nodiscard]] bool ResolvesFree(NameId name);
  void Declare(SymbolId symbol, ScopeId scope, bool initializer);
  void CatchParameter(SymbolId symbol, ScopeId scope);
  void Finish();
 private:
  [[nodiscard]] std::optional<SymbolId> ActiveBinding(std::u16string_view name);
  const FunctionTree& tree_;
  TreeCheckBudget& budget_;
  std::span<const SourceBindingPresence> presence_;
  std::vector<SymbolId> ordered_;
  std::vector<std::size_t> groups_;
  std::vector<std::size_t> group_counts_;
  std::vector<std::optional<SymbolId>> heads_;
  std::vector<std::optional<SymbolId>> next_;
  std::vector<std::optional<SymbolId>> active_;
  std::vector<std::optional<SymbolId>> previous_;
  std::vector<std::uint8_t> declared_;
  std::vector<std::uint8_t> entered_;
  std::vector<std::optional<ModuleSymbol>> modules_;
};

struct TreeScopedReference { SymbolId symbol; ScopeId scope; };
struct TreeScopedName { NameId name; ScopeId scope; };
// Nearest enclosing binding with the same spelling, indexed once by canonical
// scope activation. Absence means no enclosing shadow, not global uniqueness.
struct TreeSpellingFacts {
  std::vector<std::optional<SymbolId>> shadowed;
  std::vector<std::uint8_t> unique;
  std::vector<SourceSpellingId> bindings;
  std::vector<std::optional<SourceSpellingId>> names;
};
[[nodiscard]] TreeSpellingFacts AnalyzeTreeSpellings(const FunctionTree&,TreeCheckBudget&);
// Resolve a batch through the same spelling/activation model as structural
// checking. Indexed scope/query adjacency avoids an ancestor walk per read.
// The caller supplies a checked tree and the actual occurrence's lexical scope.
[[nodiscard]] std::vector<std::uint8_t> ResolveTreeReferences(
    const FunctionTree&, std::span<const TreeScopedReference>, TreeCheckBudget&,
    std::span<const SourceBindingPresence> = {});
[[nodiscard]] std::vector<std::uint8_t> ResolveTreeNames(
    const FunctionTree&, std::span<const TreeScopedName>, TreeCheckBudget&,
    std::span<const SourceBindingPresence> = {});

// Canonical origin arenas own contiguous part ranges, referencing only earlier
// origins. Sharing uses origin IDs, never overlapping part-list storage.
void CheckTreeOrigins(const FunctionTree& tree, TreeCheckBudget& budget);
}  // namespace abcd::source
#endif
