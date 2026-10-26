#ifndef ABCD_SOURCE_PHASE_PREDICATE_PREFIX_H_
#define ABCD_SOURCE_PHASE_PREDICATE_PREFIX_H_
#include "logical_evaluation.h"
#include "effect_order.h"
namespace abcd::source {
// Resolve original inline/module ownership as a functional forest. Operation
// storage order supplies no ownership fact; cycles cannot form source subtrees.
std::vector<analysis::OpId> IndexPhaseInlineRoots(const analysis::ValueProgram&,
    const EvaluationPlan&, TreeCheckBudget&);
struct PhasePredicatePrefixes {
  struct Range { std::size_t first = 0, count = 0; };
  std::vector<Range> ranges;
  std::vector<analysis::OpId> operations;
  EffectOrder order;
};
// Root-use equations identify exclusive decision/selected-arm dependencies.
// The supplied original regions make a retained input's predicate and arm
// observations one enclosure, including its exact phi-edge occurrence. Discovery
// without regions keeps the predicate-only index. A single indexed must-reach
// proof retains execution count/order; catch/Reference/scope admission remains
// with the common operand verifier, which recomputes this index independently.
PhasePredicatePrefixes IndexPhasePredicatePrefixes(const analysis::ValueProgram&,
    const EvaluationPlan&, std::span<const analysis::OpId> roots, TreeCheckBudget&,
    std::span<const SelectionRegion> regions = {});
}
#endif
