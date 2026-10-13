#ifndef ABCD_SOURCE_CALL_REFERENCE_H_
#define ABCD_SOURCE_CALL_REFERENCE_H_
#include "abcd/analysis/value_ir.h"
#include "abcd/source/evaluation_kind.h"
namespace abcd::source {
enum class CallReferenceError : std::uint8_t { kNotPropertyResult, kNotInline };
// Diagnostic evidence only; never an admission certificate or a rewrite hint.
struct CallReferenceEvidence {
  CallReferenceError kind;
  analysis::OpId call;
  std::optional<analysis::OpId> property;
  std::optional<EvaluationKind> placement;
  std::optional<TemporaryReason> temporary;
  std::optional<analysis::UseId> consumer;
  bool operator==(const CallReferenceEvidence&) const = default;
};
}  // namespace abcd::source
#endif
