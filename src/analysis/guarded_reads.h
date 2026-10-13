#ifndef ABCD_ANALYSIS_GUARDED_READS_H_
#define ABCD_ANALYSIS_GUARDED_READS_H_

#include "abcd/analysis/value_ir.h"

namespace abcd::analysis {
enum class GuardedReadError : std::uint8_t {
  kInput, kIdentity, kOrder, kEscape, kCatchState, kCorrespondence, kResourceLimit, kWorkLimit
};
// Raw evidence, never an initialized-cell premise or source permission. The
// caller must bind the lexical cell and prove the native TDZ declaration.
struct GuardedRead {
  OpId read, guard;
  ValueId value;
  WorkBlockId success;
  std::optional<ValueHandler> handler;
  bool operator==(const GuardedRead&) const = default;
};
struct GuardedReads {
  IrRevision revision;
  CfgRevision cfg_revision;
  std::vector<GuardedRead> reads; // read OpId order
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Requires an independently verified phased graph. Identity and completed-guard
// dominance cover all uses, including phi/catch transport. A shared finite
// lattice computes the preceding observable event; no per-read path walks.
[[nodiscard]] std::variant<GuardedReads, GuardedReadError> ProveGuardedReads(
    const ValueProgram&, SemanticBudget = {});
[[nodiscard]] std::variant<std::uint64_t, GuardedReadError> VerifyGuardedReads(
    const ValueProgram&, const GuardedReads&, SemanticBudget = {});
}  // namespace abcd::analysis
#endif
