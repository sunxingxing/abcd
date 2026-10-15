#ifndef ABCD_SOURCE_CELL_PARAMETER_DEFAULT_FAILURE_H_
#define ABCD_SOURCE_CELL_PARAMETER_DEFAULT_FAILURE_H_
#include "abcd/source/tree_failure.h"
namespace abcd::source {
enum class CellParameterDefaultError : std::uint8_t {
  kParameterIdentity, kLocalCell, kOuterBinding, kAlias, kPrefixObservation,
  kParameterOrder, kGuard, kCatchState, kBodyState, kCorrespondence,
  kResourceLimit, kWorkLimit
};
// Diagnostic identities are deliberately distinct: an allocated destination is
// not an incoming capture even if its slot number happens to be equal.
struct ParameterCellIdentity {
  analysis::OpId allocation;
  analysis::LexicalSlot slot;
  bool operator==(const ParameterCellIdentity& b) const { return allocation==b.allocation && slot.value==b.slot.value; }
};
struct DefaultCaptureIdentity {
  analysis::CapturedDepth depth;
  analysis::LexicalSlot slot;
  bool operator==(const DefaultCaptureIdentity& b) const { return depth==b.depth && slot.value==b.slot.value; }
};
struct CellParameterDefaultFailure {
  CellParameterDefaultError kind = CellParameterDefaultError::kCorrespondence;
  std::optional<std::uint32_t> parameter;
  std::optional<ParameterCellIdentity> local;
  std::optional<DefaultCaptureIdentity> outer;
  TreeLocation location;
  std::optional<analysis::UseId> use;
};
// Bounded summary only. It cannot authorize a residual rewrite or publication.
struct CellParameterDefaultDiagnostic {
  bool verified = false;
  std::uint32_t count = 0;
  CellParameterDefaultFailure evidence;
  TreeLocation initial_store, test, read, guard, fallback_store;
  std::optional<analysis::WorkBlockId> body;
};
}
#endif
