#ifndef ABCD_SOURCE_FRESH_OBJECT_FIELDS_H_
#define ABCD_SOURCE_FRESH_OBJECT_FIELDS_H_

#include <cstddef>
#include <string_view>
#include <variant>
#include <vector>

#include "abcd/abc/pool_reference.h"
#include "abcd/source/linear_body.h"

namespace abcd::source {
inline constexpr std::string_view kFieldTempPrefix = "$abcdField";

struct FreshFieldSite {
  std::size_t instruction;
  std::size_t first;
  abc::PoolIndex name;
  abc::RegisterId object;
};
struct FreshFieldPlan {
  std::vector<FreshFieldSite> sites;
  std::size_t AllocatedBytes() const {
    return sites.capacity() * sizeof(FreshFieldSite);
  }
};

// Straight-line only. A buffered object must be stored immediately into one
// register and acc overwritten before any observation. Every later receiver
// read/write ends the chain. Source emission validates the buffer is data-only.
// O(I) time and O(P) space for I instructions and P property definitions.
[[nodiscard]] std::variant<FreshFieldPlan, BodyEmitFailure>
BuildFreshFieldPlan(const abc::MethodBody& body, std::size_t frame_size,
                    std::size_t budget_bytes);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_FRESH_OBJECT_FIELDS_H_
