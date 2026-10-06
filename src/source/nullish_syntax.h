#ifndef ABCD_SOURCE_NULLISH_SYNTAX_H_
#define ABCD_SOURCE_NULLISH_SYNTAX_H_
#include "tree_structure.h"
namespace abcd::source {
// Keep the whole original postfix spine, including calls. Only access begins
// optional evaluation; later links remain mandatory (x?.f().a must throw when
// f returns nullish). Access may be a member or an indirect value invocation.
struct OptionalMemberSyntax { ExprId member; ExprId base; ExprId access; };
// Retain the original call/arguments. A member call guards its receiver and
// keeps its reference; a value call guards the stored callable and stays indirect.
struct OptionalCallSyntax { ExprId call; ExprId base; std::optional<ExprId> prefix; std::optional<ExprId> access; };
struct NullishFallbackSyntax { ExprId value; ExprId fallback; };
using NullishProjection = std::variant<OptionalMemberSyntax, OptionalCallSyntax, NullishFallbackSyntax>;
using NullishRoot = std::variant<ExprId, StmtId>;
enum class NullishForm : std::uint8_t { kOptionalMember, kOptionalCall, kFallback };
// Native nullish syntax over an unchanged verified expression/statement tree.
// Tests refer to one binding/receiver domain. An ordered reference-forming
// expression is retained as the base; keys/getters/calls keep their ownership.
struct NullishSyntax {
  std::vector<std::optional<NullishProjection>> projections;
  std::vector<std::optional<ExprId>> omitted_by;
  std::vector<std::optional<ExprId>> omitted_properties_by;
  std::vector<std::optional<NullishProjection>> statement_projections;
  std::vector<std::optional<StmtId>> statement_omitted_by;
  std::vector<std::optional<StmtId>> statement_expressions_by;
  std::vector<std::optional<StmtId>> statement_properties_by;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  [[nodiscard]] bool Retains(ExprId root, ExprId child) const;
  [[nodiscard]] ExprId Base(const FunctionTree& tree, ExprId root) const;
  [[nodiscard]] NullishForm Form(ExprId root) const;
  [[nodiscard]] const NullishProjection& Projection(NullishRoot root) const;
  [[nodiscard]] bool Retains(NullishRoot root, ExprId child) const;
  [[nodiscard]] ExprId Base(const FunctionTree& tree, NullishRoot root) const;
  [[nodiscard]] NullishForm Form(NullishRoot root) const;
  [[nodiscard]] std::optional<NullishRoot> Omission(ExprId id) const;
  [[nodiscard]] std::optional<NullishRoot> Omission(StmtId id) const;
  [[nodiscard]] std::optional<NullishRoot> Omission(PropertyId id) const;
};
// Interpret predicates over the language's null/undefined/other partition.
// Discarded statements share that domain and ordered formation. Native syntax
// retains the original effect occurrence; declarations/abrupt completions and
// additional effects keep their original statements, never disappear by shape.
// Resolve arbitrary arena order with bounded worklists, never a
// fixed guard count, branch layout or per-candidate subtree replay.
[[nodiscard]] std::variant<NullishSyntax, TreeStructureError>
PlanNullishSyntax(const FunctionTree& tree, analysis::SemanticBudget budget = {});
}
#endif
