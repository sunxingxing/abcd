#ifndef ABCD_SOURCE_HOLE_PROTOCOL_H_
#define ABCD_SOURCE_HOLE_PROTOCOL_H_

#include "abcd/analysis/value_ir.h"

namespace abcd::source {
struct HoleComparison {
  analysis::OpId operation;
  analysis::ValueId payload;
  bool equal;
};
struct HoleRethrow {
  analysis::OpId operation;
  analysis::OpId comparison;
};
struct HoleProtocol {
  std::vector<HoleComparison> comparisons;
  std::vector<HoleRethrow> rethrows;
};
enum class HoleProtocolError : std::uint8_t {
  kInput,
  kEscape,
  kUnguardedThrow,
  kResourceLimit,
  kWorkLimit
};
// Imported phased IR only. Finds closed Hole transport through copies/phis,
// identity tests with a definite Hole on the left, and rethrows on a unique
// incoming corridor from the non-Hole branch. Does not print a VM sentinel or
// upgrade any value domain. Other consumers, joins losing the guard, and
// environment/unknown VM values reject. O(V+O+U+B+E), iterative throughout.
[[nodiscard]] std::variant<HoleProtocol, HoleProtocolError> AnalyzeHoleProtocol(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
