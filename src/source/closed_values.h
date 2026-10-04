#ifndef ABCD_SOURCE_CLOSED_VALUES_H_
#define ABCD_SOURCE_CLOSED_VALUES_H_

#include "abcd/analysis/value_ir.h"

namespace abcd::source {
enum class ClosedValueError : std::uint8_t { kInput, kResourceLimit, kWorkLimit };
// Context-free language values built from closed operands without observable
// effects or abrupt completion. Caller first verifies availability/uses and the
// imported instruction semantics; queries use that same unchanged graph.
// Identity phi/copy projection is shared with
// SSA; distinct-choice phis and entry/frame/cell/heap sources are not constants.
// Closure alone never authorizes moving, replaying or printing an expression.
class ClosedValues {
 public:
  static std::variant<ClosedValues, ClosedValueError> Build(
      const analysis::ValueProgram&, analysis::SemanticBudget = {});
  bool Contains(const analysis::ValueProgram&, const analysis::ValueOperand&) const;
  bool Expression(analysis::OpId) const;
  std::size_t AllocatedBytes() const { return bytes_; }
  std::uint64_t Work() const { return work_; }
 private:
  std::vector<analysis::ValueId> roots_;
  std::vector<std::uint8_t> closed_, expressions_;
  std::size_t bytes_ = 0;
  std::uint64_t work_ = 0;
};
}  // namespace abcd::source
#endif
