#ifndef ABCD_SOURCE_BINDING_INITIALIZER_H_
#define ABCD_SOURCE_BINDING_INITIALIZER_H_
#include "tree_structure.h"
namespace abcd::source {
struct LoopSyntax;
struct NullishSyntax;
struct BindingInitializer {
  SymbolId symbol;
  ExprId assignment;
  DeclarationKind declaration;
};
struct BindingInitializers {
  std::vector<std::optional<BindingInitializer>> statements;
  std::vector<std::uint8_t> omitted;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Requires a structurally checked tree and its current loop/nullish projections.
// Native declarations at the original assignment occurrence. Empty private
// declarations can initialize in the same or a nested lexical container only
// when the first completed direct store dominates every confined observation
// and no capture/module alias escapes. DFS intervals prove lexical containment.
// The canonical AST, expressions and execution order remain unchanged; private
// syntax lifetimes may shrink. This does not recover user or captured bindings.
// Loop/nullish expression programs retain their existing representation.
// O(statements + expressions + owned children + symbols + capture references),
// bounded iterative ownership walks; no per-symbol CFG traversal or SSA merge.
[[nodiscard]] std::variant<BindingInitializers, TreeStructureError>
PlanBindingInitializers(const FunctionTree&, const LoopSyntax&, const NullishSyntax&,
    analysis::SemanticBudget budget = {});
}
#endif
