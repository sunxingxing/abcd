#ifndef ABCD_SOURCE_EFFECT_ORDER_H_
#define ABCD_SOURCE_EFFECT_ORDER_H_
#include "../analysis/value_flow.h"
namespace abcd::source {
struct EntryEffect { bool operator==(const EntryEffect&) const = default; };
struct EffectJoin {
  analysis::WorkBlockId representative;
  bool operator==(const EffectJoin&) const = default;
};
using EffectPredecessor = std::variant<EntryEffect, EffectJoin, analysis::OpId>;
enum class ExecutionAnchor : std::uint8_t { kNone, kRequired };
struct EffectOrder {
  analysis::DominatorTree dominators;
  // Exact first effect reached by all paths from method entry, including empty
  // entry blocks. This is a control fact, not the first physical operation.
  std::optional<analysis::OpId> first;
  std::vector<EffectPredecessor> before;
  // Exact next ordered effect reached by every path, before any divergence or
  // normal exit. Absence is no permission to advance an effect across control.
  std::vector<std::optional<analysis::OpId>> after;
  // All normal paths after this operation reach a real return without another
  // ordered effect or divergence. Return values remain the caller's obligation.
  // Distinct return occurrences can agree on completion without equating PCs.
  std::vector<std::uint8_t> normal_return;
  std::vector<std::uint32_t> position;
  // Phased writes become available only after their successful Commit. This
  // index keeps normal effect reachability separate from exceptional exits.
  std::vector<std::optional<analysis::WorkBlockId>> completions;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  [[nodiscard]] bool Before(const analysis::ValueProgram&, analysis::OpId,
      analysis::WorkBlockId, std::uint32_t) const;
};
[[nodiscard]] bool HasOrderedEffect(const analysis::ValueOp&);
// Normal and phased SSA. The original full-flow dominators prove successful
// availability; effect equations use ordinary edges, while catch dispatch and
// before-throw copies remain the caller's separate obligations.
// Equal effect histories pass through arbitrary pure CFG
// splits/joins. Different histories remain opaque joins, never equal counts.
// Iterative Kosaraju contracts pure dependency SCCs, including pure loops;
// effects cut dependencies. O((B+E) log B + U log U + O+V), O(B+E+O+U+V)
// storage, including shared availability/dominance. No per-object graph walks.
// execution_owners projects already checked operand subtrees into their source
// consumer event. The caller owns expression, execution-count, leaf availability
// and catch obligations; this projection alone never authorizes folding effects.
// Required anchors expose caller-selected execution points in the same must-reach
// equations, even if pure. They prove count/order, never invent language effects.
[[nodiscard]] std::variant<EffectOrder, analysis::ValueAvailabilityError>
BuildEffectOrder(const analysis::ValueProgram&, analysis::SemanticBudget = {},
    std::span<const analysis::OpId> execution_owners = {},
    std::span<const ExecutionAnchor> anchors = {});
}
#endif
