#ifndef ABCD_SOURCE_FRESH_OBJECT_FIELDS_H_
#define ABCD_SOURCE_FRESH_OBJECT_FIELDS_H_

#include <cstddef>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "abcd/abc/pool_reference.h"
#include "abcd/analysis/normal_flow.h"
#include "abcd/source/linear_body.h"

namespace abcd::source {
inline constexpr std::string_view kFieldTempPrefix = "$abcdField";

struct FreshFieldSite {
  std::size_t instruction;
  std::size_t first;
  std::variant<abc::PoolIndex, std::uint32_t> key;
  abc::RegisterId object;
};
struct FreshFieldPlan {
  std::vector<FreshFieldSite> sites;
  std::size_t AllocatedBytes() const {
    return sites.capacity() * sizeof(FreshFieldSite);
  }
};

// A fresh object must be stored immediately and acc overwritten before use.
// Multiple private receivers may remain live; reads, overwrites and unknown
// effects revoke freshness. Source emission validates data-only buffers.
// O(I + expanded register reads) time, O(R + P) scratch for R registers and
// P property definitions. The branch form resets facts at every basic block.
[[nodiscard]] std::variant<FreshFieldPlan, BodyEmitFailure>
BuildFreshFieldPlan(const abc::MethodBody& body, std::size_t frame_size,
                    std::size_t budget_bytes);
[[nodiscard]] std::variant<FreshFieldPlan, BodyEmitFailure>
BuildFreshFieldPlan(std::span<const abc::BodyInstruction> instructions,
                    std::size_t frame_size, std::size_t budget_bytes);
[[nodiscard]] std::variant<FreshFieldPlan, BodyEmitFailure>
BuildBranchFreshFieldPlan(const abc::MethodBody& body,
                          const analysis::NormalFlow& graph,
                          std::size_t frame_size, std::size_t budget_bytes);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_FRESH_OBJECT_FIELDS_H_
