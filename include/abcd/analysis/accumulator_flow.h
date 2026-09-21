#ifndef ABCD_ANALYSIS_ACCUMULATOR_FLOW_H_
#define ABCD_ANALYSIS_ACCUMULATOR_FLOW_H_

#include "abcd/analysis/block_order.h"

namespace abcd::analysis {
enum class AccumulatorState : std::uint8_t { kUnreachable, kMayBeHole, kValue };
struct AccumulatorFlow {
  BlockOrder order;
  std::vector<AccumulatorState> entries;
  std::size_t allocated_bytes = 0;
  std::uint64_t instruction_visits = 0;
};
enum class AccumulatorError {
  kExceptionRegions,
  kInvalidGraph,
  kOrder,
  kUnsupportedInstruction,
  kReadBeforeValue,
  kResourceLimit
};
struct AccumulatorFailure {
  AccumulatorError kind;
  abc::CodeOffset offset;
  std::optional<OrderError> order_error = {};
};
struct AccumulatorBudget {
  std::size_t bytes = 64 * 1024 * 1024;
};

// Requires a validated body and its unchanged BuildNormalFlow result, with
// language-valued registers at entry. Supported register writes must also
// consume language values; internal environments/other operations are rejected.
// Normal DAG analysis only: rejects handlers, resume edges and reachable loops.
// Hole writes kill initialization; all incoming reachable paths must supply a
// value before a read. No branch constant-folding or permission to emit Hole.
// Success proves only this property, NOT full instruction/source equivalence.
// O(I+B+E), bounded vector capacities including simultaneous order scratch.
[[nodiscard]] std::variant<AccumulatorFlow, AccumulatorFailure>
AnalyzeAcyclicAccumulator(const abc::MethodBody& body, const NormalFlow& graph,
                          std::uint64_t frame_size,
                          AccumulatorBudget budget = {});
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_ACCUMULATOR_FLOW_H_
