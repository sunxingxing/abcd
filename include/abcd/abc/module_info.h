#ifndef ABCD_ABC_MODULE_INFO_H_
#define ABCD_ABC_MODULE_INFO_H_

#include "abcd/abc/metadata_index.h"

namespace abcd::abc {

struct ModuleRequestId {
  std::uint16_t value = 0;
  auto operator<=>(const ModuleRequestId&) const = default;
};
struct ModuleImport {
  EntityOffset local;
  EntityOffset imported;
  ModuleRequestId request;
};
struct NamespaceImport {
  EntityOffset local;
  ModuleRequestId request;
};
struct LocalExport {
  EntityOffset local;
  EntityOffset exported;
};
struct IndirectExport {
  EntityOffset exported;
  EntityOffset imported;
  ModuleRequestId request;
};
enum class ModuleRequestPhase : std::uint8_t { kEager, kLazy };
struct ModuleInfo {
  // The pinned VM skips this leading word; it is not a trusted byte length.
  std::uint32_t encoded_count = 0;
  std::vector<EntityOffset> requests;
  std::vector<ModuleImport> imports;
  std::vector<NamespaceImport> namespace_imports;
  std::vector<LocalExport> local_exports;
  std::vector<IndirectExport> indirect_exports;
  std::vector<ModuleRequestId> star_exports;
  EntityOffset end;
  // Empty means the optional wire table is absent (all requests eager).
  std::vector<ModuleRequestPhase> request_phases;
};
enum class ModuleError {
  kUnsupportedProfile,
  kFileChanged,
  kRead,
  kTruncated,
  kReference,
  kRequestIndex,
  kResourceLimit,
  kRequestPhases,
};
struct ModuleFailure {
  ModuleError kind;
  EntityOffset offset;
};
struct ModuleBudget {
  std::size_t bytes = 64 * 1024 * 1024;
};
using ModuleResult = std::variant<ModuleInfo, ModuleFailure>;

// Direct module entity, not an ordinary tagged literal array. Preserves file
// order; validates reference ranges but leaves string payloads lazy. O(entries)
// time/storage, all vector capacities charged to the additional caller budget.
[[nodiscard]] ModuleResult ReadModuleInfo(const corpus::AbcFile& file,
                                          const Header& header,
                                          EntityOffset offset,
                                          ModuleBudget budget = {},
                                          std::optional<EntityOffset> phases = {});

}  // namespace abcd::abc
#endif  // ABCD_ABC_MODULE_INFO_H_
