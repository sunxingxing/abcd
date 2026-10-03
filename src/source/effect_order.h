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
struct EffectOrder {
  analysis::DominatorTree dominators;
  std::vector<EffectPredecessor> before;
  std::vector<std::uint32_t> position;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
  [[nodiscard]] bool Before(const analysis::ValueProgram&, analysis::OpId,
      analysis::WorkBlockId, std::uint32_t) const;
};
[[nodiscard]] bool HasOrderedEffect(const analysis::ValueOp&);
// Normal SSA only. Equal effect histories pass through arbitrary pure CFG
// splits/joins. Different histories remain opaque joins, never equal counts.
// Iterative Kosaraju contracts pure dependency SCCs, including pure loops;
// effects cut dependencies. O((B+E) log B + U log U + O+V), O(B+E+O+U+V)
// storage, including shared availability/dominance. No per-object graph walks.
[[nodiscard]] std::variant<EffectOrder, analysis::ValueAvailabilityError>
BuildEffectOrder(const analysis::ValueProgram&, analysis::SemanticBudget = {});
}
#endif
