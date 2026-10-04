#ifndef ABCD_SOURCE_NULLISH_SYNTAX_H_
#define ABCD_SOURCE_NULLISH_SYNTAX_H_
#include "tree_structure.h"
namespace abcd::source {
struct OptionalMemberSyntax { ExprId member; };
struct NullishFallbackSyntax { ExprId value; ExprId fallback; };
using NullishProjection = std::variant<OptionalMemberSyntax, NullishFallbackSyntax>;
enum class NullishForm : std::uint8_t { kOptionalMember, kFallback };
// Native nullish syntax over an unchanged verified expression tree.
// Only pure tests of the same stable binding/receiver may be discharged;
// computed keys, getters and outside calls retain their original ownership.
struct NullishSyntax {
  std::vector<std::optional<NullishProjection>> projections;
  std::vector<std::optional<ExprId>> omitted_by;
  std::vector<std::optional<ExprId>> omitted_properties_by;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  [[nodiscard]] bool Retains(ExprId root, ExprId child) const;
  [[nodiscard]] ExprId Base(const FunctionTree& tree, ExprId root) const;
  [[nodiscard]] NullishForm Form(ExprId root) const;
};
// Interpret predicates over the language's null/undefined/other partition.
// Resolve arbitrary expression-arena order with one bounded worklist, never a
// fixed guard count, branch layout or per-candidate subtree replay.
[[nodiscard]] std::variant<NullishSyntax, TreeStructureError>
PlanNullishSyntax(const FunctionTree& tree, analysis::SemanticBudget budget = {});
}
#endif
