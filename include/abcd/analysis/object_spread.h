#ifndef ABCD_ANALYSIS_OBJECT_SPREAD_H_
#define ABCD_ANALYSIS_OBJECT_SPREAD_H_

#include "abcd/analysis/scalar.h"

namespace abcd::analysis {
// OH7 interpreter-inl.cpp:5464 / runtime_stubs-inl.h:453. On success acc is
// exactly object; on failure acc has its pre-Commit value. Never erase the
// operation when using this normal-result identity.
struct CopyProperties {
  abc::RegisterId object;
  ScalarValue source;
};
[[nodiscard]] std::variant<CopyProperties, ScalarError> LiftCopyProperties(
    const abc::DecodedInstruction& instruction, std::uint64_t frame);
}  // namespace abcd::analysis
#endif
