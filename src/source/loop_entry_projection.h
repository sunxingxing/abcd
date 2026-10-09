#ifndef ABCD_SOURCE_LOOP_ENTRY_PROJECTION_H_
#define ABCD_SOURCE_LOOP_ENTRY_PROJECTION_H_
#include "loop_declarations.h"
#include "recovery_flow.h"
namespace abcd::source {
struct LoopEntryOccurrence {
  std::optional<StmtId> root;
  std::optional<SourcePosition> enclosing;
};
struct LoopEntryProjection {
  std::vector<std::optional<BindingInitializer>> initializers;
  std::vector<std::optional<DeclarationCompletion>> completions;
  std::vector<SourceBindingScope> bindings;
  std::vector<std::optional<SourcePosition>> occurrences;
  std::vector<std::optional<NativeLoopAllocation>> allocations;
  std::vector<std::optional<StmtId>> owners;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
// Adapt one complete grammar product to semantic input, without mutating the
// recovery plan. Prefix assignments/effects retain their canonical environment;
// only actual declarators own a loop environment. This publishes initial allocation order, not dynamic cell
// identity or per-iteration clone. Current/trial use the same adapter.
[[nodiscard]] std::variant<LoopEntryProjection,TreeStructureError> ProjectLoopEntryFlow(
    const LoopDeclarations&,std::span<const std::optional<BindingInitializer>>,
    std::span<const std::optional<DeclarationCompletion>>,
    std::span<const SourceBindingScope> original_bindings,const SourceScopeProjection&,
    std::span<const LoopEntryOccurrence>,analysis::SemanticBudget={});
}
#endif
