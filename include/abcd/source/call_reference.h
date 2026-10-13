#ifndef ABCD_SOURCE_CALL_REFERENCE_H_
#define ABCD_SOURCE_CALL_REFERENCE_H_
#include "abcd/analysis/value_ir.h"
#include "abcd/source/evaluation_kind.h"
namespace abcd::source {
enum class CallReferenceError : std::uint8_t { kNotPropertyResult, kNotInline };
enum class GuardedCallError : std::uint8_t { kIdentity, kUse, kOrder, kReference, kCatchState, kCorrespondence };
struct GuardedCallFailure {
  GuardedCallError kind = GuardedCallError::kOrder;
  analysis::OpId call, read, guard;
  analysis::UseId use;
  std::optional<abc::CodeOffset> read_pc, read_working_pc, guard_pc, guard_working_pc;
  bool operator==(const GuardedCallFailure&) const = default;
};
// Diagnostic evidence only; never an admission certificate or a rewrite hint.
struct CallReferenceEvidence {
  CallReferenceError kind;
  analysis::OpId call;
  std::optional<analysis::OpId> property;
  std::optional<EvaluationKind> placement;
  std::optional<TemporaryReason> temporary;
  std::optional<analysis::UseId> consumer;
  std::optional<GuardedCallFailure> checked;
  bool operator==(const CallReferenceEvidence&) const = default;
};
}  // namespace abcd::source
#endif
