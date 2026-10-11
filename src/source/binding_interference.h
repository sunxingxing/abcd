#ifndef ABCD_SOURCE_BINDING_INTERFERENCE_H_
#define ABCD_SOURCE_BINDING_INTERFERENCE_H_
#include "flow_liveness.h"
namespace abcd::source {
// A preference supplies candidates only, never historical non-interference
// or value-equality evidence. Every proposal is checked against current facts.
struct BindingPreference {SymbolId first,second;};
struct BindingPreferences {
  std::vector<BindingPreference> pairs;
  std::size_t allocated_bytes=0;
  std::uint64_t work=0;
};
[[nodiscard]] std::variant<BindingPreferences,TreeStructureError>
FindBindingPreferences(const RecoveryFlow&,analysis::SemanticBudget = {});
struct BindingAffinity { SymbolId first, second; bool interference = false; };
struct BindingConflict { SymbolId first, second; auto operator<=>(const BindingConflict&) const = default; };
struct BindingInterference {
  std::vector<BindingAffinity> affinities;
  // Include non-affinity conflicts: otherwise contracting two accepted pairs
  // could hide interference between their other members.
  std::vector<BindingConflict> conflicts;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Consume shared live-in facts and successful definitions, never AST shapes.
// The producer owns propagation; this consumer records the complete temporary
// interference graph as well as copy affinities.
// Copy affinities are proposals only: lexical visibility, capture and storage
// lifetime must still be proved before changing a binding. Finally dispatch is
// a conservative union of pending continuations, never an omitted cleanup.
[[nodiscard]] std::variant<BindingInterference, TreeStructureError>
FindBindingInterference(const RecoveryFlow&, const FlowLiveness&,
    analysis::SemanticBudget = {},std::span<const BindingPreference> = {});
}
#endif
