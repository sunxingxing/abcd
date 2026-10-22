#ifndef ABCD_SOURCE_EVALUATION_KIND_H_
#define ABCD_SOURCE_EVALUATION_KIND_H_
#include <cstdint>
namespace abcd::source {
enum class EvaluationKind : std::uint8_t {
  kRemoved, kRoot, kInline, kInitializer, kEnvironment, kModuleGuard, kElidedPure, kAlias, kNestedLiteral, kClassProjection, kReceiverGuard, kCellGuard, kPhiInput, kLogicalLeft, kLogicalValue, kPredicateSnapshot, kArrayCursor, kGuardedCellRead, kForInEvent, kConcurrentNotification, kForOfEvent, kTemplateConstruction
};
enum class TemporaryReason : std::uint8_t {
  kMultipleUses, kEffectSnapshot, kPhiJoin, kParallelCopyCycle,
  kCrossRegionValue, kReferenceSnapshot, kUnprovedInline, kPredicateSnapshot,
  kExceptionSnapshot, kPendingCompletion, kInternalSentinel, kNumericUpdate,
  kDecisionPredicate, kEnumerationKey
};
}  // namespace abcd::source
#endif
