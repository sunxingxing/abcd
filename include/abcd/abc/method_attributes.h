#ifndef ABCD_ABC_METHOD_ATTRIBUTES_H_
#define ABCD_ABC_METHOD_ATTRIBUTES_H_

#include <cstddef>

#include "abcd/abc/header.h"

namespace abcd::abc {
enum class FunctionKind : std::uint8_t {
  kNone,
  kBaseConstructor,
  kArrow,
  kGenerator,
  kAsync,
  kAsyncGenerator,
  kAsyncArrow,
  kConcurrent
};
inline constexpr std::size_t kFunctionKindCount =
    static_cast<std::size_t>(FunctionKind::kConcurrent) + 1;
struct MethodRegionIndex {
  std::uint16_t value = 0;
};
struct MethodAttributes {
  FunctionKind kind;
  bool sendable = false;
  std::uint8_t access = 0;
  MethodRegionIndex pool_region;
};
enum class MethodAttributeError { kUnsupportedProfile, kUnknownKind };

// Decode the pinned runtime's initial kind, not final source syntax. Function
// creation sites may refine the kind; NONE is not evidence for ordinary JS
// function syntax. The region index is retained, not validated against a file.
// Only known dynamic container profiles are accepted. This does not prove
// historical execution semantics or compatibility of sendable functions.
[[nodiscard]] std::variant<MethodAttributes, MethodAttributeError>
DecodeMethodAttributes(std::uint32_t access_flags, ProfileId profile);
}  // namespace abcd::abc
#endif  // ABCD_ABC_METHOD_ATTRIBUTES_H_
