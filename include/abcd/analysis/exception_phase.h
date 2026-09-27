#ifndef ABCD_ANALYSIS_EXCEPTION_PHASE_H_
#define ABCD_ANALYSIS_EXCEPTION_PHASE_H_

#include <compare>
#include <cstdint>

namespace abcd::analysis {
struct ExceptionNodeId {
  std::uint32_t value = 0;
  auto operator<=>(const ExceptionNodeId&) const = default;
};
enum class ExceptionPhase : std::uint8_t { kBefore, kCommit, kCatch };
}  // namespace abcd::analysis
#endif
