#ifndef ABCD_ABC_DEBUG_SCOPES_H_
#define ABCD_ABC_DEBUG_SCOPES_H_

#include "abcd/abc/debug_info.h"

namespace abcd::abc {
struct DebugLocal {
  std::int32_t reg = 0;
  EntityOffset name;
  EntityOffset type;
  std::optional<EntityOffset> signature;
  std::uint32_t start = 0;
  std::optional<std::uint32_t> end;
};
struct DebugPosition {
  std::uint32_t pc = 0;
  std::optional<std::uint32_t> line;
  std::uint32_t column = 0;
  std::optional<EntityOffset> file;
  std::optional<EntityOffset> source_code;
};
struct DebugScopes {
  std::vector<DebugLocal> locals;
  std::vector<DebugPosition> positions;
  std::uint32_t terminal_pc = 0;
  std::optional<EntityOffset> terminal_file;
  std::optional<EntityOffset> terminal_source_code;
};
enum class DebugScopeError {
  kRestartUnsupported,
  kLocalState,
  kPcRange,
  kLineRange,
  kResourceLimit,
};
struct DebugScopeFailure {
  EntityOffset offset;
  std::variant<MetadataError, DebugScopeError> error;
};
struct DebugScopeBudget {
  std::size_t bytes = 4 * 1024 * 1024;
  std::size_t events = 1024 * 1024;
};
using DebugScopeResult = std::variant<DebugScopes, DebugScopeFailure>;
// Debug ranges are encoded evidence, not source let/const declarations. Only
// explicit END_LOCAL proves an endpoint. Signed registers retain wire meaning.
// PC bounds are checked against code size, not decoded instruction boundaries.
[[nodiscard]] DebugScopeResult ReadDebugScopes(const corpus::AbcFile& file,
                                               const Header& header,
                                               const DebugInfo& debug,
                                               std::uint32_t code_bytes,
                                               DebugScopeBudget budget = {});
}  // namespace abcd::abc
#endif
