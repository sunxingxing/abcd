#ifndef ABCD_SOURCE_TREE_STRUCTURE_H_
#define ABCD_SOURCE_TREE_STRUCTURE_H_

#include "abcd/source/source_tree.h"

namespace abcd::source {
struct GlobalScope;
// Source-name retention in one projection, distinct from value liveness/TDZ.
enum class SourceBindingPresence : std::uint8_t {kRetained,kElided};
struct SourceSpellingId {std::uint32_t value=0;auto operator<=>(const SourceSpellingId&)const=default;};
struct TreeStructure {
  analysis::IrRevision value_revision;
  analysis::CfgRevision cfg_revision;
  std::size_t maximum_depth = 0;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  // Checker temporaries are dead on return. The returned scopes are still live.
  std::size_t retained_bytes = 0;
  std::size_t input_bytes = 0;
  // Indexed by ExprId, recorded during the same ownership/scope traversal.
  // Required for creation-point enclosure checks across function boundaries;
  // a function-wide union of bindings incorrectly includes sibling blocks.
  std::vector<ScopeId> expression_scopes;
};
// Independently checks syntax ownership, lexical name resolution, declaration
// census and provenance shape. Includes tree-owned capacity and scratch in the
// byte allowance unless an exact still-charged BorrowedPrintTrees owner is held. This report is NOT an IR-equivalence or publication proof:
// TDZ/initialization, effects, CFG edges and ABI facts need semantic validation.
[[nodiscard]] std::variant<TreeStructure, TreeStructureError>
VerifyTreeStructure(const FunctionTree& tree,
                    analysis::SemanticBudget budget = {}, const GlobalScope* destination = nullptr);
// Conditional syntax only. Ordinary verification/printing rejects this class
// draft until actual creator enclosure and recovery ports have been checked.
[[nodiscard]] std::variant<TreeStructure, TreeStructureError>
VerifyClassInitializerStructure(const FunctionTree&,
                                analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
