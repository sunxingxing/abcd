#ifndef ABCD_ANALYSIS_INTERNAL_VALUE_FAILURE_H_
#define ABCD_ANALYSIS_INTERNAL_VALUE_FAILURE_H_

#include "abcd/analysis/register_ssa.h"
#include "abcd/analysis/linear_lexicals.h"
#include "abcd/analysis/value_kinds.h"

namespace abcd::analysis {
// An input-domain refusal, not permission to replace a VM marker by undefined.
// Mixed domains remain mixed; never claim the solver proved one exact marker.
enum class InternalValueReason : std::uint8_t {
  kMissingFact, kEmpty, kHole, kEnvironment, kOtherInternal,
  kPropertyEnumerator, kMixed, kEnumerationProtocol,
  kCyclicCellInitialization, kCellInitialization, kCellPreservation, kPatternRecord
};
constexpr InternalValueReason InternalReason(std::optional<ValueKinds> kinds) {
  if (!kinds) return InternalValueReason::kMissingFact;
  if (kinds->IsEmpty()) return InternalValueReason::kEmpty;
  if (*kinds == ValueKinds::Hole()) return InternalValueReason::kHole;
  if (*kinds == ValueKinds::Environment()) return InternalValueReason::kEnvironment;
  if (*kinds == ValueKinds::OtherInternal()) return InternalValueReason::kOtherInternal;
  if (*kinds == ValueKinds::PropertyEnumerator()) return InternalValueReason::kPropertyEnumerator;
  if (*kinds == ValueKinds::PatternRecord()) return InternalValueReason::kPatternRecord;
  if (kinds->IsLanguage()) return InternalValueReason::kEnumerationProtocol;
  return InternalValueReason::kMixed;
}
struct InternalValueEvidence {
  InternalValueReason reason;
  std::optional<SsaValueId> value;
  InstructionId observer;
  std::optional<std::uint32_t> read_ordinal;
  // These are original semantic SSA identities, BEFORE Value IR OpId/UseId
  // exist. A phi/entry has no invented producer instruction or PC. Copy
  // producers stay copies; this witness does not pretend to be a def-use proof.
  std::optional<std::variant<MethodEntry, InstructionValue, PhiValue>> producer;
  std::optional<ValueKinds> kinds;
  std::optional<LocalCell> cell;
  std::optional<InstructionId> allocation;
  std::optional<InstructionId> store;
};
}  // namespace abcd::analysis
#endif
