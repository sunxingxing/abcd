#ifndef ABCD_SOURCE_EVALUATION_DISPOSITION_H_
#define ABCD_SOURCE_EVALUATION_DISPOSITION_H_
#include "abcd/source/evaluation_kind.h"
namespace abcd::source {
// Consume verified placements; this table does not prove or weaken them.
// Only a boundary anchor can enter a decision prefix without executable syntax.
// Enclosing projections and environment declarations need their block owner.
enum class EvaluationDisposition : std::uint8_t {
  kExpression, kAnchor, kEnclosingAnchor, kEnvironment, kInitializer,
  kCheckedRead, kRemoved, kProtocol
};
constexpr EvaluationDisposition Disposition(EvaluationKind kind) {
  switch (kind) {
    case EvaluationKind::kConcurrentNotification:
    case EvaluationKind::kForOfEvent:
    case EvaluationKind::kForInEvent: return EvaluationDisposition::kProtocol;
    case EvaluationKind::kRemoved: return EvaluationDisposition::kRemoved;
    case EvaluationKind::kCellGuard:
    case EvaluationKind::kAlias:
    case EvaluationKind::kElidedPure: return EvaluationDisposition::kAnchor;
    case EvaluationKind::kClassProjection:
    case EvaluationKind::kArrayCursor: return EvaluationDisposition::kEnclosingAnchor;
    case EvaluationKind::kEnvironment: return EvaluationDisposition::kEnvironment;
    case EvaluationKind::kInitializer: return EvaluationDisposition::kInitializer;
    case EvaluationKind::kGuardedCellRead: return EvaluationDisposition::kCheckedRead;
    case EvaluationKind::kRoot:
    case EvaluationKind::kInline:
    case EvaluationKind::kModuleGuard:
    case EvaluationKind::kNestedLiteral:
    case EvaluationKind::kReceiverGuard:
    case EvaluationKind::kPhiInput:
    case EvaluationKind::kLogicalLeft:
    case EvaluationKind::kLogicalValue:
    case EvaluationKind::kPredicateSnapshot: return EvaluationDisposition::kExpression;
  }
  return EvaluationDisposition::kRemoved;
}
} // namespace abcd::source
#endif
