#ifndef ABCD_SOURCE_LOOP_SYNTAX_H_
#define ABCD_SOURCE_LOOP_SYNTAX_H_
#include "tree_structure.h"
namespace abcd::source {
class TreeCheckBudget;
// A statement prefix expressible as ordered effects and a Boolean normal
// completion and one typed transfer owner (break or continue).
// This is a syntax domain, not an opcode/effect whitelist: every expression,
// including await/yield/getters, retains its complete existing occurrence.
enum class LoopCompletionTransfer : std::uint8_t { kBreak, kContinue };
struct LoopCompletionMeaning {
  bool expressible = false;
  bool normal = false;
  std::optional<StmtId> exit;
  std::optional<bool> literal;
  LoopCompletionTransfer transfer = LoopCompletionTransfer::kBreak;
};
struct LoopConditionSyntax {
  std::uint32_t header_count;
  // A branch's continuing arm remains an original block in the loop body.
  // Its complementary arm must complete only with the owned loop break.
  std::optional<StmtId> body;
};
struct LoopTransitionSyntax { StmtId block; std::uint32_t first; std::uint32_t end; };
struct LoopInitializerRoot { StmtId statement; BlockBoundary boundary; };
struct LoopInitializerSyntax { std::uint32_t first; std::uint32_t end; };
struct LoopSyntax {
  std::vector<LoopCompletionMeaning> meanings;
  std::vector<std::optional<LoopConditionSyntax>> conditions;
  std::vector<std::optional<LoopTransitionSyntax>> transitions;
  std::vector<std::optional<std::uint32_t>> tail_starts;
  std::vector<LoopInitializerRoot> initializer_roots;
  std::vector<std::optional<LoopInitializerSyntax>> initializers;
  std::vector<std::optional<StmtId>> initializer_owners;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Project the already verified tree's completion algebra into while/for syntax.
// Sequencing is comma/short-circuit AND, decisions are conditional expressions;
// no value/phi/effect reordering, duplicated subtrees or inferred transfer targets.
// Crossed blocks must bind no names and own no label. Declaration/try/return/
// continue in headers remain statements. Transition programs can contain only
// their exact owned continue; all other completions stay in the original body.
// Declaration/try prefixes lack an expression representation of scope/handlers.
// These are coverage gaps, not VM invariants. Disjoint prefix/transition scans
// plus an indexed def-use closure cost O(N + R log N) time and O(N) storage,
// N = statement/expression/child/symbol/scope counts, R = dependency reads.
// Independent of arena/CFG/register layout; no per-variable graph walks.
// Initializer roots form a disjoint entry stream through transparent blocks.
// A shared write census selects condition-read bindings written inside the loop;
// prior definitions/intervening effects retain source order and exact ownership.
[[nodiscard]] std::variant<LoopSyntax, TreeStructureError>
PlanLoopSyntax(const FunctionTree& tree, analysis::SemanticBudget budget = {});
// Internal continuation ownership and carried-binding dependency projection.
// Moves an ordered program, never an opcode or a numeric-update pattern.
void PlanLoopTransitions(const FunctionTree&, LoopSyntax&, TreeCheckBudget&,
    std::span<const std::uint8_t> bound);
// Entry expressions retain their ordered occurrences and scope/handler owner.
// Transparent blocks can separate roots from the loop; no same-block premise.
void PlanLoopInitializers(const FunctionTree&, LoopSyntax&, TreeCheckBudget&,
    std::span<const std::uint8_t> bound);
}
#endif
