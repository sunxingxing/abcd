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

// Scope activation uses one slot per distinct spelling. It resolves the exact
// lexical symbol in O(1), including shadowing, without per-use parent walks.
class TreeScopeCheck {
 public:
  TreeScopeCheck(const FunctionTree& tree, TreeCheckBudget& budget);
  void Enter(ScopeId scope);
  void Leave(ScopeId scope);
  void Reference(SymbolId symbol, bool assignment = false);
  void Declare(SymbolId symbol, ScopeId scope, bool initializer);
  void Finish();
 private:
  const FunctionTree& tree_;
  TreeCheckBudget& budget_;
  std::vector<SymbolId> ordered_;
  std::vector<std::size_t> groups_;
  std::vector<std::optional<SymbolId>> heads_;
  std::vector<std::optional<SymbolId>> next_;
  std::vector<std::optional<SymbolId>> active_;
  std::vector<std::optional<SymbolId>> previous_;
  std::vector<std::uint8_t> declared_;
  std::vector<std::uint8_t> entered_;
};

// Canonical origin arenas own contiguous part ranges, referencing only earlier
// origins. Sharing uses origin IDs, never overlapping part-list storage.
void CheckTreeOrigins(const FunctionTree& tree, TreeCheckBudget& budget);
}  // namespace abcd::source
#endif
