#ifndef ABCD_ANALYSIS_PHASED_VALUE_RULES_H_
#define ABCD_ANALYSIS_PHASED_VALUE_RULES_H_

#include <limits>
#include "phased_value_import.h"
#include "operand_view.h"

namespace abcd::analysis::detail {
using PhaseError = ValueImportError;
inline void RequirePhase(bool condition, PhaseError error,
                         std::optional<abc::CodeOffset> pc = {}) {
  if (!condition) throw ValueImportFailure{error, pc};
}
class PhaseQuota {
 public:
  explicit PhaseQuota(SemanticBudget budget) : remaining_(budget) {}
  void Work(std::uint64_t count = 1) {
    RequirePhase(count <= remaining_.work, PhaseError::kWorkLimit);
    remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    RequirePhase(count <= std::numeric_limits<std::uint32_t>::max() &&
        count <= remaining_.bytes / width, PhaseError::kResourceLimit);
    remaining_.bytes -= count * width;
  }
  template <typename T> void Reserve(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T));
    values.reserve(count);
    if (values.capacity() > count) Admit(values.capacity() - count, sizeof(T));
  }
  SemanticBudget Remaining() const { return remaining_; }
 private:
  SemanticBudget remaining_;
};
inline bool SameSlot(const ScalarSlot& a, const ScalarSlot& b) {
  if (a.index() != b.index()) return false;
  const auto* reg = std::get_if<abc::RegisterId>(&a);
  return !reg || *reg == std::get<abc::RegisterId>(b);
}
FlowEdgeKind PhaseEdgeKind(ExceptionArcKind kind);
OperandView PhaseOperands(const SemanticInstruction& instruction, PhaseQuota& quota);
// Traverses every kind of successor from entry; environment absence is checked
// against this topology, never used as a reachability oracle.
std::vector<PhaseReachability> CheckPhaseInput(const PhasedValueInput& input, PhaseQuota& quota);
ValueOperand PhaseLiteral(const ScalarValue& value);
}  // namespace abcd::analysis::detail
#endif
