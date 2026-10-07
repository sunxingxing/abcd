#ifndef ABCD_SOURCE_BINDING_INTERFERENCE_H_
#define ABCD_SOURCE_BINDING_INTERFERENCE_H_
#include "recovery_flow.h"
namespace abcd::source {
struct BindingAffinity { SymbolId first, second; bool interference = false; };
struct BindingInterference {
  std::vector<BindingAffinity> affinities;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Backward may-liveness consumes the shared RecoveryFlow, never AST shapes.
// Completion and
// exceptional edges precede writes; a failed conversion never commits a store.
// Copy affinities are proposals only: lexical visibility, capture and storage
// lifetime must still be proved before changing a binding. Finally dispatch is
// a conservative union of pending continuations, never an omitted cleanup.
[[nodiscard]] std::variant<BindingInterference, TreeStructureError>
FindBindingInterference(const RecoveryFlow&,
    analysis::SemanticBudget = {});
}
#endif
