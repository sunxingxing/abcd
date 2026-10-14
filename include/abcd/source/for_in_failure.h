#ifndef ABCD_SOURCE_FOR_IN_FAILURE_H_
#define ABCD_SOURCE_FOR_IN_FAILURE_H_
#include "abcd/analysis/value_ir.h"
namespace abcd::source {
enum class ForInError : std::uint8_t {
  kIdentity, kEscape, kAcquisitionOrder, kAdvance, kDone, kKeyBinding,
  kActivation, kExit, kCatchState, kExceptionCorrespondence, kCorrespondence,
  kResourceLimit, kWorkLimit
};
// Missing locations stay missing. These are original working-IR identities;
// callers map PCs through origins, never through a neighbouring instruction.
struct ForInFailure {
  ForInError kind;
  std::optional<analysis::OpId> acquisition, operation;
  std::optional<analysis::ValueId> value;
  std::optional<analysis::UseId> use;
  std::optional<analysis::WorkBlockId> block;
  std::optional<analysis::EdgeId> edge;
  bool operator==(const ForInFailure&) const = default;
};
} // namespace abcd::source
#endif
