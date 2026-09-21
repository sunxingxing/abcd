#include "abcd/abc/method_attributes.h"

#include <array>
#include <cstdlib>

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
}  // namespace
int main() {
  using namespace abcd::abc;
  const std::array expected{
      FunctionKind::kNone,       FunctionKind::kBaseConstructor,
      FunctionKind::kArrow,      FunctionKind::kGenerator,
      FunctionKind::kAsync,      FunctionKind::kAsyncGenerator,
      FunctionKind::kAsyncArrow, FunctionKind::kConcurrent};
  for (const auto profile :
       {ProfileId::kDynamic9, ProfileId::kDynamic1102, ProfileId::kDynamic1202,
        ProfileId::kDynamic1206, ProfileId::kDynamic1301,
        ProfileId::kDynamic2400}) {
    for (std::uint32_t kind = 0; kind < 256; ++kind) {
      for (const std::uint32_t region : {0u, 1u, 200u, 201u, 65535u}) {
        constexpr std::uint32_t kAccess = 0xa5;
        const auto result = DecodeMethodAttributes(
            (region << 16) | (kind << 8) | kAccess, profile);
        if (kind >= 16) {
          Check(std::get<MethodAttributeError>(result) ==
                MethodAttributeError::kUnknownKind);
          continue;
        }
        const auto attributes = std::get<MethodAttributes>(result);
        Check(attributes.kind == expected[kind % expected.size()]);
        Check(attributes.sendable == (kind >= 8));
        Check(attributes.access == kAccess);
        Check(attributes.pool_region.value == region);
      }
    }
  }
  for (const auto unsupported : {ProfileId::kUnknown, ProfileId::kStatic0107})
    Check(std::get<MethodAttributeError>(DecodeMethodAttributes(
              0, unsupported)) == MethodAttributeError::kUnsupportedProfile);
}
