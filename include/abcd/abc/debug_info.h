#ifndef ABCD_ABC_DEBUG_INFO_H_
#define ABCD_ABC_DEBUG_INFO_H_

#include "abcd/abc/metadata_index.h"

namespace abcd::abc {
struct DebugInfo {
  std::uint32_t line_start = 0;
  std::vector<std::optional<EntityOffset>> parameters;
  EntityOffset constant_pool;
  std::uint32_t constant_pool_bytes = 0;
  EntityOffset line_program;
  EntityOffset end;
};
struct DebugBudget {
  std::size_t bytes = 1024 * 1024;
};
using DebugResult = std::variant<DebugInfo, MetadataError>;
// Parse one entity in O(parameters), without loading its constant pool or line
// program. Parameter order is debug evidence, not a source signature mapping.
[[nodiscard]] DebugResult ReadDebugInfo(const corpus::AbcFile& file,
                                        const Header& header,
                                        EntityOffset offset,
                                        DebugBudget budget = {});
}  // namespace abcd::abc
#endif
