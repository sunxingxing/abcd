#ifndef ABCD_ANALYSIS_NAMED_READ_H_
#define ABCD_ANALYSIS_NAMED_READ_H_

#include "abcd/abc/instruction.h"
#include "abcd/abc/pool_reference.h"

namespace abcd::analysis {
enum class PropertyLookup : std::uint8_t { kObject, kSuper };
struct NamedRead {
  abc::PoolIndex name;
  PropertyLookup lookup = PropertyLookup::kObject;
};
enum class NamedReadError { kUnsupportedOpcode, kMalformedInstruction };

// Modern LDOBJBYNAME: acc is receiver and receives the value only on success.
// May invoke getters/proxies or throw; not a pure or own-property-only read.
// Name is a method-regional string slot, never a direct file offset. Legacy
// ID32 narrowing requires separate runtime-profile evidence. Decoded input
// required. Constant time/storage, no allocation or runtime lookup.
[[nodiscard]] std::variant<NamedRead, NamedReadError> LiftNamedRead(
    const abc::DecodedInstruction& instruction);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_NAMED_READ_H_
