#ifndef ABCD_ANALYSIS_NAMED_WRITE_H_
#define ABCD_ANALYSIS_NAMED_WRITE_H_

#include "abcd/abc/pool_reference.h"
#include "abcd/analysis/named_read.h"
#include "abcd/abc/register_access.h"

namespace abcd::analysis {
struct NamedWrite {
  abc::PoolIndex name;
  abc::RegisterId object;
  PropertyLookup lookup = PropertyLookup::kObject;
};
enum class NamedWriteError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};

// Modern STOBJBYNAME: throwing named store, receiver in a register, value in
// acc. Acc and the receiver register are preserved; setter/Proxy effects and
// throws are not discarded. Not an own-property definition. The name is a
// regional string pool slot. Native JS equivalence needs resolved-name
// validation: the pinned name fast path bypasses ordinary element storage for
// numeric names. Validated decoded input; O(1), no allocations.
[[nodiscard]] std::variant<NamedWrite, NamedWriteError> LiftNamedWrite(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_NAMED_WRITE_H_
