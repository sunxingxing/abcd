#ifndef ABCD_SOURCE_REST_OWNERSHIP_H_
#define ABCD_SOURCE_REST_OWNERSHIP_H_
#include "abcd/analysis/value_ir.h"
namespace abcd::source {
enum class RestOwnershipError : std::uint8_t { kFrame, kAllocation, kArgumentUse, kOrder, kResourceLimit, kWorkLimit };
struct RestOwnership { std::size_t allocated_bytes; std::uint64_t work; };
// Verified normal or phased IR and its original body/frame. Native rest hoists one array
// allocation only across unobservable argument transport. Original argument
// scalar uses, slot overwrites, snapshots and repeated allocation stay distinct.
// No register numbering, prologue length or instruction-adjacency template.
[[nodiscard]] std::variant<RestOwnership, RestOwnershipError> ProveRestOwnership(
    const analysis::ValueProgram&, const abc::MethodBody&, const abc::FrameLayout&,
    std::uint16_t first, analysis::SemanticBudget = {});
}
#endif
