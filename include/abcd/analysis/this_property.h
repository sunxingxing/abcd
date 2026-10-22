#ifndef ABCD_ANALYSIS_THIS_PROPERTY_H_
#define ABCD_ANALYSIS_THIS_PROPERTY_H_

#include "abcd/abc/instruction.h"
#include "abcd/abc/pool_reference.h"
#include "abcd/abc/register_access.h"

namespace abcd::analysis {
enum class ThisPropertyKind : std::uint8_t {
  kReadName, kReadValue, kWriteName, kWriteValue
};
constexpr bool WritesThisProperty(ThisPropertyKind kind) {
  return kind == ThisPropertyKind::kWriteName || kind == ThisPropertyKind::kWriteValue;
}
constexpr bool NamedThisProperty(ThisPropertyKind kind) {
  return kind == ThisPropertyKind::kReadName || kind == ThisPropertyKind::kWriteName;
}
struct ThisProperty {
  ThisPropertyKind kind;
  std::optional<abc::PoolIndex> name;
  std::optional<abc::RegisterId> key;
};
enum class ThisPropertyError : std::uint8_t {
  kUnsupportedOpcode, kMalformedInstruction, kRegisterRange
};
// OH7 interpreter-inl.cpp:7056-7547: current frame.thisObj, never the acc or
// optional ABI snapshot. Get/strict Set preserves exact key/value operands.
// Allocation-free. Source projection additionally proves initialized owner scope.
[[nodiscard]] std::variant<ThisProperty, ThisPropertyError> LiftThisProperty(
    const abc::DecodedInstruction&, std::uint64_t frame);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_THIS_PROPERTY_H_
