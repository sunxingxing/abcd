#ifndef ABCD_SOURCE_SELECTION_CONSUMER_H_
#define ABCD_SOURCE_SELECTION_CONSUMER_H_
#include "logical_regions.h"
#include "evaluation_plan.h"
namespace abcd::source {
// A discovered continuation is not permission to consume its result. Retained
// values stop the route without changing their phi, reads or local snapshots.
enum class SelectionExitKind : std::uint8_t { kUnique, kPredicateKeep, kSnapshot, kMaterialized };
struct SelectionConsumer {
  SelectionExitKind kind = SelectionExitKind::kMaterialized;
  SelectionExitReason reason = SelectionExitReason::kNoUse;
  std::optional<analysis::UseId> use, witness, keep;
  std::optional<analysis::EdgeId> edge;
  // Only admitted exits supply an operand occurrence to the interval proof.
  std::optional<analysis::UseId> InlineUse() const {
    return kind == SelectionExitKind::kUnique || kind == SelectionExitKind::kPredicateKeep ? use : std::nullopt;
  }
};
SelectionConsumer FindSelectionConsumer(const analysis::ValueProgram&, const SelectionRegion&,
    TreeCheckBudget&, const SelectionRegion* logical_left = nullptr);
}
#endif
