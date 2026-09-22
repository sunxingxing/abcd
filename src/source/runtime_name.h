#ifndef ABCD_SOURCE_RUNTIME_NAME_H_
#define ABCD_SOURCE_RUNTIME_NAME_H_
#include <optional>
#include <string_view>

#include "abcd/abc/metadata_index.h"

namespace abcd::source {
// Pinned MethodLiteral::ParseFunctionNameView. Non-ASCII uses a UTF-16-length
// byte view upstream; reject it rather than invent Unicode substring semantics.
inline std::optional<std::string_view> RuntimeName(
    const abc::MethodInfo& method) {
  constexpr unsigned char kAsciiLimit = 0x80;
  std::string_view name = method.name;
  if (name.empty() || name.size() != method.name_utf16_length) return {};
  for (const unsigned char byte : name)
    if (byte == 0 || byte >= kAsciiLimit) return {};
  if (name.front() == '#') {
    name.remove_prefix(name.find_last_of('#') + 1);
    const auto suffix = name.find_last_of('^');
    if (suffix != std::string_view::npos) name = name.substr(0, suffix);
  }
  return name;
}
}  // namespace abcd::source
#endif
