#ifndef ABCD_ANALYSIS_INSTRUCTION_RANK_H_
#define ABCD_ANALYSIS_INSTRUCTION_RANK_H_

#include <compare>
#include <cstdint>

namespace abcd::analysis {
// A normal-DAG linearization coordinate, not dominance or a runtime timestamp.
struct InstructionRank {
  std::uint32_t value;
  auto operator<=>(const InstructionRank&) const = default;
};
}  // namespace abcd::analysis
#endif
