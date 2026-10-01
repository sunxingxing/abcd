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
  std::size_t specialized_edges = 0;
};
// One owned candidate per batch, never one clone per value. Budget includes
// both graphs plus simultaneous scratch. Failure leaves the original unchanged.
// Requires verified input effects/identity. Checks flow, anchored identity
// equations, effects and origins before committing. Shared primitive-copy/jump
// tails (including jump-only tails after elision) may be specialized per incoming
// edge with matching outgoing phi inputs. Pure tail cycles are retained;
// this advances CFG revision. No effect motion, load duplication or load CSE.
[[nodiscard]] std::variant<ValueSimplifyStats, ValueSimplifyError> SimplifyValues(
    ValueProgram& graph, IrRevision expected, SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif
