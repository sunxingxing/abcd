#ifndef ABCD_STRUCTURE_PACKAGE_SOURCE_H_
#define ABCD_STRUCTURE_PACKAGE_SOURCE_H_

#include <optional>
#include <string_view>

namespace abcd::structure {
struct PackageSource {
  std::u16string_view entry;
  std::u16string_view package;
  std::u16string_view version;
  std::u16string_view path;
};
// v7 ace_ets2bundle SourceMapGenerator::genKey wire format, as embedded in
// ABC sourceFile by module_mode.ts. Does not read a source map artifact.
std::optional<PackageSource> ParsePackageSource(std::u16string_view name);
}  // namespace abcd::structure
#endif
