#ifndef ABCD_ANALYSIS_SEMANTIC_REGION_H_
#define ABCD_ANALYSIS_SEMANTIC_REGION_H_

#include <memory>
#include <variant>
#include <vector>

#include "abcd/analysis/semantic_ir.h"
#include "abcd/analysis/exception_regions.h"

namespace abcd::analysis {
inline constexpr std::size_t kMaxSemanticRegionDepth = 128;
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
struct RegionLoop {
  BlockId header;
  std::unique_ptr<RegionNode> body;
};
struct RegionTry {
  TryId id;
  std::unique_ptr<RegionNode> body;
  std::unique_ptr<RegionNode> handler;
  // Pure compiler jump over the handler, represented by a zero-width source
  // anchor. Its target is the lexical continuation after this whole region.
  std::optional<InstructionId> skip;
};
struct RegionNode {
  std::variant<RegionBlock, RegionLabel, RegionSequence, RegionLoop, RegionTry> value;
};
struct SemanticRegionTree {
  RegionNode root;
  std::vector<std::uint32_t> order_positions;
  std::size_t allocated_bytes = 0;
  std::size_t catches = 0;
};
enum class RegionError { kUnreachable, kCycle, kBackwardEdge, kDepth,
                         kResourceLimit, kExceptionShape };
struct RegionFailure {
  RegionError kind;
  abc::CodeOffset pc;
};
using RegionResult = std::variant<SemanticRegionTree, RegionFailure>;

// Acyclic forward edges become breaks out of labels surrounding preceding
// blocks. Shared blocks appear once, and effectful instructions are never
// duplicated. Cycles with laminar, single-entry block-order intervals become
// explicit loops. Cross-entry/overlapping loops and exceptional edges refuse.
[[nodiscard]] RegionResult BuildSemanticRegions(
    const SemanticMethod& method, std::size_t budget_bytes);

// Same unchanged body/phased-flow certificate and BuildNormalFlow topology.
// Produces the common RegionNode tree without copying/rebasing instructions.
// Single-entry laminar loops confined to one unchanged catch scope are allowed;
// enclosing labels preserve normal exits across loops and catches.
// The only elided gap is a single proven jump over its handler. No value or
// exception-input admission is implied. Bounded depth, O(I+B+R+E) construction.
[[nodiscard]] RegionResult BuildExceptionRegionTree(
    const ExceptionFlow& flow, const ExceptionRegions& regions,
    const NormalFlow& normal, std::size_t budget_bytes);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_SEMANTIC_REGION_H_
