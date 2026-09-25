#ifndef ABCD_ANALYSIS_VALUE_SIMPLIFY_H_
#define ABCD_ANALYSIS_VALUE_SIMPLIFY_H_

#include "abcd/analysis/value_ir.h"

namespace abcd::analysis {
enum class ValueSimplifyError : std::uint8_t {
  kStaleRevision, kRevisionLimit, kInvalidGraph, kCopyCycle, kUnsupportedFlow,
  kWorkLimit, kResourceLimit
};
struct ValueSimplifyStats {
  IrRevision revision;
  std::size_t replaced_uses = 0;
  std::size_t removed_ops = 0;
  std::size_t removed_phis = 0;
};
// One owned candidate per batch, never one clone per value. Budget includes
// both graphs plus simultaneous scratch. Failure leaves the original unchanged.
// Requires verified input effects; flow and structural validation here do
// not produce a ValuesVerified publication certificate. No motion or load CSE.
[[nodiscard]] std::variant<ValueSimplifyStats, ValueSimplifyError> SimplifyValues(
    ValueProgram& graph, IrRevision expected, SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
