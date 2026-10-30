#ifndef ABCD_ANALYSIS_NAMED_READ_H_
#define ABCD_ANALYSIS_NAMED_READ_H_

#include "abcd/abc/instruction.h"
#include "abcd/abc/pool_reference.h"
#include "abcd/abc/register_access.h"

namespace abcd::analysis {
enum class PropertyLookup : std::uint8_t { kObject, kSuper };
struct NamedRead {
  abc::PoolIndex name;
  PropertyLookup lookup = PropertyLookup::kObject;
  // Empty: acc receiver (current forms). DEPRECATED_LDOBJBYNAME reads a register.
  std::optional<abc::RegisterId> receiver;
};
enum class NamedReadError { kUnsupportedOpcode, kMalformedInstruction };

// Modern LDOBJBYNAME: acc is receiver and receives the value only on success.
// May invoke getters/proxies or throw; not a pure or own-property-only read.
// Name is a method-regional string slot, never a direct file offset. The
// pinned interpreter narrows DEPRECATED_LDOBJBYNAME's ID32 to a uint16 pool
// slot (interpreter-inl.cpp:6572); only ids that survive narrowing
// unchanged are admitted. Decoded input required. Constant time/storage, no
// allocation or runtime lookup.
[[nodiscard]] std::variant<NamedRead, NamedReadError> LiftNamedRead(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_NAMED_READ_H_
