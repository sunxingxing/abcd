#ifndef ABCD_SOURCE_DERIVED_VALUE_STATES_H_
#define ABCD_SOURCE_DERIVED_VALUE_STATES_H_
#include "derived_projection.h"
#include "abcd/analysis/capture_effect.h"
namespace abcd::source {
enum class DerivedValueState : std::uint8_t { kBottom, kUndefined, kSuper, kConstructor, kOther };
struct DerivedValueStates {
  std::vector<DerivedValueState> values;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Same original input and current work graph. Caller independently inspected
// every supplied same-body preservation site. Sparse local SSA selects stores;
// actual load/store addresses, effects and operands are checked again here.
// No captured incoming cell is inferred initialized or immutable. Each origin
// changes at most twice (bottom -> concrete -> other), bounding propagation.
[[nodiscard]] std::variant<DerivedValueStates, DerivedProjectionError> BuildDerivedValueStates(
    const FileFunction&, analysis::OpId call,
    std::span<const analysis::CapturePreservation>, analysis::SemanticBudget);
}
#endif
