#ifndef ABCD_SOURCE_HOLE_PROTOCOL_H_
#define ABCD_SOURCE_HOLE_PROTOCOL_H_

#include "abcd/source/evaluation_plan_cause.h"
#include <algorithm>
#include "abcd/analysis/value_ir.h"

namespace abcd::source {
struct ForOfEvaluationPlan;
struct HoleComparison {
  analysis::OpId operation;
  analysis::ValueId payload;
  bool equal;
  bool operator==(const HoleComparison&) const = default;
};
struct HoleRethrow {
  analysis::OpId operation;
  analysis::OpId comparison;
  bool operator==(const HoleRethrow&) const = default;
};
struct HoleProtocol {
  std::vector<HoleComparison> comparisons;
  std::vector<HoleRethrow> rethrows;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
inline analysis::BinaryOperator HoleSourceOperator(const HoleProtocol* protocol,
    analysis::OpId operation, analysis::BinaryOperator original) {
  if (!protocol) return original;
  const auto found = std::lower_bound(protocol->comparisons.begin(), protocol->comparisons.end(), operation,
      [](const auto& comparison, auto id) { return comparison.operation < id; });
  if (found == protocol->comparisons.end() || found->operation != operation) return original;
  return found->equal ? analysis::BinaryOperator::kStrictEqual : analysis::BinaryOperator::kStrictNotEqual;
}

// Normal IR admits only closed copy transport to definite-Hole class heritage.
// Phased IR additionally finds closed Hole transport through copies/phis,
// identity tests with a definite Hole on the left, and rethrows on a unique
// incoming corridor from the non-Hole branch. Does not print a VM sentinel or
// upgrade any value domain. Other consumers, joins losing the guard, and
// environment/unknown VM values reject. O(V+O+U+B+E), iterative throughout.
[[nodiscard]] std::variant<HoleProtocol, HoleProtocolError> AnalyzeHoleProtocol(
    const analysis::ValueProgram& graph, analysis::SemanticBudget budget = {}, EvaluationInputSite* failure = nullptr,
    const ForOfEvaluationPlan* for_of = nullptr);
}  // namespace abcd::source
#endif
