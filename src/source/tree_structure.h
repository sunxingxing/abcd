#ifndef ABCD_SOURCE_TREE_STRUCTURE_H_
#define ABCD_SOURCE_TREE_STRUCTURE_H_

#include "abcd/source/source_tree.h"

namespace abcd::source {
enum class TreeStructureError : std::uint8_t {
  kRange, kOwnership, kScope, kBinding, kName, kTarget, kOrigin,
  kUnsupportedSyntax, kResourceLimit, kWorkLimit
};
struct TreeStructure {
  analysis::IrRevision value_revision;
  analysis::CfgRevision cfg_revision;
  std::size_t maximum_depth = 0;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Independently checks syntax ownership, lexical name resolution, declaration
// census and provenance shape. Includes tree-owned capacity and scratch in the
// byte allowance. This report is NOT an IR-equivalence or publication proof:
// TDZ/initialization, effects, CFG edges and ABI facts need semantic validation.
[[nodiscard]] std::variant<TreeStructure, TreeStructureError>
VerifyTreeStructure(const FunctionTree& tree,
                    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
