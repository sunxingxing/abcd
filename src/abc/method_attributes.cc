#include "abcd/abc/method_attributes.h"

#include <array>

namespace abcd::abc {
std::variant<MethodAttributes, MethodAttributeError> DecodeMethodAttributes(
    std::uint32_t access_flags, ProfileId profile) {
  switch (profile) {
    case ProfileId::kDynamic9:
    case ProfileId::kDynamic1102:
    case ProfileId::kDynamic1202:
    case ProfileId::kDynamic1206:
    case ProfileId::kDynamic1301:
    case ProfileId::kDynamic2400:
      break;
    case ProfileId::kUnknown:
    case ProfileId::kStatic0107:
      return MethodAttributeError::kUnsupportedProfile;
    default:
      return MethodAttributeError::kUnsupportedProfile;
  }
  // runtime_core file_items.h / IndexAccessor and JSPandaFile::GetFunctionKind.
  // Do not interpret the upper region-index bits as access modifiers or kind.
  constexpr unsigned kAccessBits = 8;
  constexpr unsigned kKindBits = 8;
  constexpr std::uint32_t kByteMask = 0xff;
  constexpr std::uint32_t kSendableMask = 1u << 3;
  constexpr std::array kinds{
      FunctionKind::kNone,       FunctionKind::kBaseConstructor,
      FunctionKind::kArrow,      FunctionKind::kGenerator,
      FunctionKind::kAsync,      FunctionKind::kAsyncGenerator,
      FunctionKind::kAsyncArrow, FunctionKind::kConcurrent};
  const auto encoded = (access_flags >> kAccessBits) & kByteMask;
  const auto base = encoded & ~kSendableMask;
  if (base >= kinds.size()) return MethodAttributeError::kUnknownKind;
  return MethodAttributes{
      kinds[base],
      (encoded & kSendableMask) != 0,
      static_cast<std::uint8_t>(access_flags & kByteMask),
      {static_cast<std::uint16_t>(access_flags >> (kAccessBits + kKindBits))}};
}
}  // namespace abcd::abc
