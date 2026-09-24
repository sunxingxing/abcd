#ifndef ABCD_SOURCE_NAMED_SCOPE_H_
#define ABCD_SOURCE_NAMED_SCOPE_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "abcd/source/local_lexical_body.h"

namespace abcd::source {
struct NamedScopes {
  std::vector<std::uint8_t> admitted;
  std::uint64_t work_units = 0;
  std::size_t AllocatedBytes() const { return admitted.capacity(); }
};

// Pinned v7.0 pandagen.cpp emits named environments only for debug builds;
// ScopeInfoExtractor uses names only for debugger lookups. Validate every
// referenced literal/string before omitting that metadata operation. Ordinary
// lexical cell semantics remain in the plan (named_scope_test.cc).
[[nodiscard]] std::variant<NamedScopes, BodyEmitFailure> ValidateNamedScopes(
    const abc::MethodBody& body, std::uint64_t frame_size,
    const MethodConstants* constants, std::size_t bytes,
    std::uint64_t work_limit);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_NAMED_SCOPE_H_
