#ifndef ABCD_SOURCE_SLOT_LIVENESS_H_
#define ABCD_SOURCE_SLOT_LIVENESS_H_

#include "semantic_ast_input.h"

namespace abcd::source {
enum class SlotAccess : std::uint8_t { kNone, kRead, kWrite };
struct SlotLiveness {
  std::vector<SlotAccess> accesses;
  std::vector<std::uint8_t> after;
};
// Final emitted operations, not original SSA. Unknown owners count as reads.
// Analyze one selected slot per invocation, never invoke once per register.
std::variant<SlotLiveness, SourceFailure> FindSlotLiveness(
    const AstMethod& method, const SourceAst& ast, analysis::ScalarSlot slot,
    std::size_t budget_bytes);
}  // namespace abcd::source
#endif
