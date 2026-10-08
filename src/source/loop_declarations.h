#ifndef ABCD_SOURCE_LOOP_DECLARATIONS_H_
#define ABCD_SOURCE_LOOP_DECLARATIONS_H_
#include "binding_initializer.h"
#include "loop_syntax.h"
namespace abcd::source {
struct LoopDeclarationStart { StmtId loop; std::optional<std::uint32_t> first; };
struct LoopDeclarator { StmtId initializer, declaration; };
struct LoopDeclarations {
  std::vector<LoopDeclarationStart> starts;
  std::vector<StmtId> demoted;
  std::vector<LoopDeclarator> declarators;
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
    analysis::SemanticBudget budget={});
}
#endif
