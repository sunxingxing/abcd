#ifndef ABCD_ANALYSIS_SEMANTIC_REGION_H_
#define ABCD_ANALYSIS_SEMANTIC_REGION_H_

#include <memory>
#include <variant>
#include <vector>

#include "abcd/analysis/semantic_ir.h"

namespace abcd::analysis {
struct RegionNode;
struct RegionBlock {
  BlockId id;
};
struct RegionLabel {
  BlockId exit;
  std::unique_ptr<RegionNode> body;
};
struct RegionSequence {
  std::vector<RegionNode> children;
};
struct RegionNode {
  std::variant<RegionBlock, RegionLabel, RegionSequence> value;
};
struct SemanticRegionTree {
  RegionNode root;
  std::vector<std::uint32_t> order_positions;
  std::size_t allocated_bytes = 0;
};
enum class RegionError { kUnreachable, kCycle, kBackwardEdge, kDepth,
                         kResourceLimit };
struct RegionFailure {
  RegionError kind;
  abc::CodeOffset pc;
};
using RegionResult = std::variant<SemanticRegionTree, RegionFailure>;

// Acyclic forward edges become breaks out of labels surrounding preceding
// blocks. Shared blocks appear once, and effectful instructions are never
// duplicated. Exceptional edges and loops require separate region forms.
[[nodiscard]] RegionResult BuildSemanticRegions(
    const SemanticMethod& method, std::size_t budget_bytes);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_SEMANTIC_REGION_H_
