#ifndef ABCD_SOURCE_EDGE_COPIES_H_
#define ABCD_SOURCE_EDGE_COPIES_H_

#include "abcd/analysis/value_ir.h"

namespace abcd::source {
struct CopyTempId { std::uint32_t value; auto operator<=>(const CopyTempId&) const = default; };
struct ParallelCopy { analysis::ValueId destination; analysis::ValueOperand source; };
using CopyTarget = std::variant<analysis::ValueId, CopyTempId>;
using CopySource = std::variant<analysis::ValueOperand, CopyTempId>;
struct CopyStep { CopyTarget destination; CopySource source; };
struct EdgeCopies {
  std::vector<CopyStep> steps;
  std::uint32_t temporaries = 0;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class CopyError : std::uint8_t {
  kDuplicateDestination, kDestination, kTemporary, kValue, kResourceLimit, kWorkLimit
};
// One parallel bundle for private provisional phi bindings, not observable
// lexical cells or arbitrary property stores. ValueIds here denote locations
// holding their pre-edge values. Caller must prove edge placement/availability.
// Ready queue + dependency counts + cycle save: O(C log C), O(C) storage for C
// assignments. No pairwise ready search or repeated graph scan per cycle.
[[nodiscard]] std::variant<EdgeCopies, CopyError> BuildEdgeCopies(
    std::span<const ParallelCopy> copies, analysis::SemanticBudget budget = {});
// Independent symbolic execution, not planner replay. Requires exact final
// parallel values and forbids writes outside destinations/private temporaries.
[[nodiscard]] std::variant<std::uint64_t, CopyError> VerifyEdgeCopies(
    std::span<const ParallelCopy> copies, const EdgeCopies& plan,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
