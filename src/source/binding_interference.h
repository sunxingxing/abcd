#ifndef ABCD_SOURCE_BINDING_INTERFERENCE_H_
#define ABCD_SOURCE_BINDING_INTERFERENCE_H_
#include "flow_liveness.h"
namespace abcd::source {
struct BindingAffinity { SymbolId first, second; bool interference = false; };
struct BindingInterference {
  std::vector<BindingAffinity> affinities;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Consume shared live-in facts and successful definitions, never AST shapes.
// The producer owns propagation; this consumer only records pair interference.
// Copy affinities are proposals only: lexical visibility, capture and storage
// lifetime must still be proved before changing a binding. Finally dispatch is
// a conservative union of pending continuations, never an omitted cleanup.
[[nodiscard]] std::variant<BindingInterference, TreeStructureError>
FindBindingInterference(const RecoveryFlow&, const FlowLiveness&,
    analysis::SemanticBudget = {});
}
#endif
