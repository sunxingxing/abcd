#ifndef ABCD_ANALYSIS_OWN_WRITE_H_
#define ABCD_ANALYSIS_OWN_WRITE_H_
#include "abcd/abc/pool_reference.h"
#include "abcd/abc/register_access.h"
namespace abcd::analysis {
enum class OwnWriteKind { kStore, kDefineField };
struct OwnWrite {
  OwnWriteKind kind;
  abc::PoolIndex name;
  abc::RegisterId object;
  std::uint16_t cache_slot = 0;
};
// Computed own store preserves acc; key coercion and descriptor changes may throw.
struct ComputedOwnWrite {
  abc::RegisterId object;
  abc::RegisterId key;
  std::uint16_t cache_slot;
};
enum class OwnWriteError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};
// STOWNBYNAME differs from DEFINEFIELDBYNAME/DEFINEPROPERTYBYNAME; the latter
// two share pinned DefineField. All read receiver and acc, preserve them on
// normal completion, and may throw.
// No proof of receiver=this, declaration placement or JS source equivalence.
[[nodiscard]] std::variant<OwnWrite, OwnWriteError> LiftOwnWrite(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
[[nodiscard]] std::variant<ComputedOwnWrite, OwnWriteError> LiftComputedOwnWrite(
    const abc::DecodedInstruction&, std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif
