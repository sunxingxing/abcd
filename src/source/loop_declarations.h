#ifndef ABCD_SOURCE_LOOP_DECLARATIONS_H_
#define ABCD_SOURCE_LOOP_DECLARATIONS_H_
#include "binding_initializer.h"
#include "loop_syntax.h"
namespace abcd::source {
struct LoopDeclarationStart { StmtId loop; std::optional<std::uint32_t> first; };
struct LoopDeclarator { StmtId initializer, declaration; };
// The complete current entry projection, distinct from the commit delta.
// An absent loop means an expression prefix in its enclosing environment.
struct LoopEntryEnvironment { StmtId statement; std::optional<StmtId> loop; };
// ECMA-262 2025 14.7.4.2: instantiate every bound name before any RHS.
struct LoopEnvironmentEntry {StmtId loop,entry;};
struct LoopEnvironmentBinding {StmtId loop;SymbolId binding;};
struct LoopDeclarations {
  std::vector<LoopDeclarationStart> starts;
  std::vector<StmtId> demoted;
  std::vector<LoopDeclarator> declarators;
  std::vector<LoopEntryEnvironment> entries;
  // Complete native groups, including no-op revisions: all bound cells are
  // allocated before the first retained declarator, not before omitted roots.
  std::vector<LoopEnvironmentEntry> environments;
  std::vector<LoopEnvironmentBinding> bindings;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Consume one immutable projection. Initializer ownership is disjoint in the
// verified LoopSyntax; an ordered expression prefix stays at its occurrence,
// followed by the suffix representable as native for-let declarators. This
// grammar pass neither infers lifetimes nor deletes evaluations. The caller
// invalidates analyses once, then applies the complete delta without queries.
[[nodiscard]] std::variant<LoopDeclarations,TreeStructureError> PlanLoopDeclarations(
    const LoopSyntax&,const BindingInitializers&,
    std::span<const std::optional<StmtId>> declarations,
    analysis::SemanticBudget budget={},std::span<const std::uint8_t> omitted={},
    std::span<const std::optional<BindingInitializer>> initializers={});
}
#endif
