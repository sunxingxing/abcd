#ifndef ABCD_ANALYSIS_OWN_WRITE_H_
#define ABCD_ANALYSIS_OWN_WRITE_H_
#include "abcd/abc/pool_reference.h"
#include "abcd/abc/register_access.h"
namespace abcd::analysis {
// Pinned WITHNAMESET defines the property before mutating the function name.
// This is not an assignment or a name-neutral own definition.
enum class PropertyNameSetting : std::uint8_t { kNone, kPropertyKey };
enum class OwnWriteKind { kStore, kDefineField };
struct OwnWrite {
  OwnWriteKind kind;
  abc::PoolIndex name;
  abc::RegisterId object;
  std::uint16_t cache_slot = 0;
  PropertyNameSetting name_setting = PropertyNameSetting::kNone;
};
// Computed own store preserves acc; key coercion and descriptor changes may throw.
struct ComputedOwnWrite {
  abc::RegisterId object;
  abc::RegisterId key;
  std::uint16_t cache_slot;
  PropertyNameSetting name_setting = PropertyNameSetting::kNone;
  OwnWriteKind kind = OwnWriteKind::kStore;
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
