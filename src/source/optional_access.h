#ifndef ABCD_SOURCE_OPTIONAL_ACCESS_H_
#define ABCD_SOURCE_OPTIONAL_ACCESS_H_
#include "tree_structure.h"
namespace abcd::source {
// Native optional-member syntax over an unchanged verified expression tree.
// Only pure tests of the same stable binding/receiver may be discharged;
// computed keys, getters and outside calls retain their original ownership.
struct OptionalAccessSyntax {
  std::vector<std::optional<ExprId>> members;
  std::vector<std::optional<ExprId>> omitted_by;
  std::vector<std::optional<ExprId>> omitted_properties_by;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Interpret predicates over the language's null/undefined/other partition.
// Resolve arbitrary expression-arena order with one bounded worklist, never a
// fixed guard count, branch layout or per-candidate subtree replay.
[[nodiscard]] std::variant<OptionalAccessSyntax, TreeStructureError>
PlanOptionalAccess(const FunctionTree& tree, analysis::SemanticBudget budget = {});
}
#endif
