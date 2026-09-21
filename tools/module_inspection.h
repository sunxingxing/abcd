#ifndef ABCD_TOOLS_MODULE_INSPECTION_H_
#define ABCD_TOOLS_MODULE_INSPECTION_H_

#include <iosfwd>
#include <optional>
#include <string_view>
#include <nlohmann/json.hpp>

#include "abcd/abc/metadata_index.h"

namespace abcd::tools {
struct ModuleCounts {
  std::uint64_t merged_files = 0;
  std::uint64_t bundle_files = 0;
  std::uint64_t checked_records = 0;
  std::uint64_t modules = 0;
  std::uint64_t module_requests = 0;
  std::uint64_t module_imports = 0;
  std::uint64_t module_exports = 0;
  std::uint64_t module_strings = 0;
  std::uint64_t failed_module_records = 0;
  ModuleCounts& operator+=(const ModuleCounts& other);
};
nlohmann::json SerializeCounts(const ModuleCounts& counts);
ModuleCounts InspectModules(const corpus::AbcFile& file,
                            const abc::Header& header,
                            const abc::MetadataIndex& index,
                            std::ostream& diagnostics,
                            std::optional<std::string_view> bundle_descriptor = {});
}  // namespace abcd::tools
#endif
