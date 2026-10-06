#ifndef ABCD_SOURCE_LOOP_SYNTAX_H_
#define ABCD_SOURCE_LOOP_SYNTAX_H_
#include "tree_structure.h"
namespace abcd::source {
// A statement prefix expressible as ordered effects and a Boolean normal
// completion. The only abrupt completion it may contain is this owned break.
// This is a syntax domain, not an opcode/effect whitelist: every expression,
// including await/yield/getters, retains its complete existing occurrence.
struct LoopHeaderMeaning {
  bool expressible = false;
  bool normal = false;
  std::optional<StmtId> exit;
  std::optional<bool> literal;
};
struct LoopConditionSyntax {
  std::uint32_t header_count;
  // A branch's continuing arm remains an original block in the loop body.
  // Its complementary arm must complete only with the owned loop break.
  std::optional<StmtId> body;
};
struct LoopSyntax {
  std::vector<LoopHeaderMeaning> meanings;
  std::vector<std::optional<LoopConditionSyntax>> conditions;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Project the already verified tree's completion algebra into native while.
// Sequencing is comma/short-circuit AND, decisions are conditional expressions;
// no value/phi/effect reordering, duplicated subtrees or inferred transfer targets.
// Crossed blocks must bind no names and own no label. Declaration/try/return/
// continue prefixes remain statements: this expression representation does not
// model their scope or completion. These are coverage gaps, not VM invariants.
// One postorder census plus disjoint loop prefixes: O(statements + children +
// symbols + scopes) time/storage, independent of arena/CFG/register layout.
[[nodiscard]] std::variant<LoopSyntax, TreeStructureError>
PlanLoopSyntax(const FunctionTree& tree, analysis::SemanticBudget budget = {});
}
#endif
