#ifndef ABCD_ANALYSIS_PRIVATE_PROPERTY_H_
#define ABCD_ANALYSIS_PRIVATE_PROPERTY_H_

#include <cstdint>
#include <variant>

#include "abcd/abc/instruction.h"
#include "abcd/abc/register_access.h"

namespace abcd::analysis {
struct PrivateNameRef {
  std::uint8_t inline_cache;
  std::uint16_t level;
  std::uint16_t slot;
};
struct PrivateLoad { PrivateNameRef name; };
struct PrivateStore {
  PrivateNameRef name;
  abc::RegisterId object;
};
struct PrivateHas { PrivateNameRef name; };
enum class PrivatePropertyError {
  kUnsupportedOpcode,
  kMalformedInstruction,
  kRegisterRange
};
using PrivatePropertyResult = std::variant<PrivateLoad, PrivateStore,
                                           PrivateHas, PrivatePropertyError>;

// The v7 private-name identity is resolved through the current lexical env at
// (level, slot). These fields are not a source spelling or class-member proof.
// Load/has read and replace acc; store reads acc as value and a register as
// object, preserves acc on normal completion. Every operation may throw.
[[nodiscard]] PrivatePropertyResult LiftPrivateProperty(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_PRIVATE_PROPERTY_H_
