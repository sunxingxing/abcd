#ifndef ABCD_SOURCE_GROUPING_PLAN_H_
#define ABCD_SOURCE_GROUPING_PLAN_H_

#include "tree_print_tokens.h"
#include "tree_checks.h"
#include <span>

namespace abcd::source {
enum class PrinterPolicy : std::uint8_t { kReadableV1 = 1 };
enum class GroupingReason : std::uint8_t {
  kNone, kPrecedence, kStatementStart, kDirective, kNoIn,
  kArrowBody, kChainBoundary, kUnknown
};
// Decisions belong to uses, not ExprIds: projection may print an expression
// in several different grammatical contexts without changing its identity.
struct GroupingContext {
  TreeOperatorContext operand;
  bool statement_start = false;
  bool directive_position = false;
  bool allow_in = true;
  bool arrow_body = false;
  bool chain_boundary = false;
};
struct GroupingOccurrence { ExprId expression; GroupingContext context; };
struct ExpressionSyntaxFacts {
  bool statement_hazard = false;
  bool string_literal = false;
  bool contains_in = false;
  bool object_literal = false;
  auto operator<=>(const ExpressionSyntaxFacts&) const = default;
};
[[nodiscard]] bool StatementIdentifierGrouping(std::u16string_view, const GroupingContext&);
[[nodiscard]] ExpressionSyntaxFacts ExpressionHeadFacts(const TreeExpression&);
struct GroupingDecision {
  GroupingReason reason = GroupingReason::kNone;
  [[nodiscard]] bool Parenthesized() const { return reason != GroupingReason::kNone; }
  auto operator<=>(const GroupingDecision&) const = default;
};
[[nodiscard]] GroupingDecision ReadableGrouping(const TreeExpression&, const GroupingContext&,
    ExpressionSyntaxFacts = {});
struct GroupingPlan {
  PrinterPolicy policy = PrinterPolicy::kReadableV1;
  std::vector<ExpressionSyntaxFacts> facts;
  std::vector<GroupingDecision> occurrences;
};
[[nodiscard]] GroupingPlan BuildGroupingPlan(const FunctionTree&,
    std::span<const GroupingOccurrence>, TreeCheckBudget&);
[[nodiscard]] bool VerifyGroupingPlan(const FunctionTree&,
    std::span<const GroupingOccurrence>, const GroupingPlan&, TreeCheckBudget&);
}  // namespace abcd::source
#endif
