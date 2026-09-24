#include "package_source.h"

#include <array>

namespace abcd::structure {
std::optional<PackageSource> ParsePackageSource(std::u16string_view name) {
  constexpr std::size_t kFields = 4;
  std::array<std::u16string_view, kFields> fields;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    const auto end = name.find(u'|');
    if ((i + 1 == fields.size()) != (end == std::u16string_view::npos)) return {};
    fields[i] = name.substr(0, end);
    if (fields[i].empty()) return {};
    if (end != std::u16string_view::npos) name.remove_prefix(end + 1);
  }
  return PackageSource{fields[0], fields[1], fields[2], fields[3]};
}
}  // namespace abcd::structure
