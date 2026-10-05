#ifndef ABCD_SOURCE_PHASE_PREDICATE_PREFIX_H_
#define ABCD_SOURCE_PHASE_PREDICATE_PREFIX_H_
#include "logical_evaluation.h"
#include "effect_order.h"
namespace abcd::source {
struct PhasePredicatePrefixes {
  struct Range { std::size_t first = 0, count = 0; };
  std::vector<Range> ranges;
  std::vector<analysis::OpId> operations;
  EffectOrder order;
};
// Root-use equations identify exclusive decision dependencies. A single indexed
// must-reach proof retains their execution count/order across arbitrary pure CFG.
// Catch/Reference/leaf obligations remain with the common operand verifier.
PhasePredicatePrefixes IndexPhasePredicatePrefixes(const analysis::ValueProgram&,
    const EvaluationPlan&, std::span<const analysis::OpId> roots, TreeCheckBudget&);
}
#endif
