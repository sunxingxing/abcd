#ifndef ABCD_SOURCE_CHECKED_READ_EVENT_H_
#define ABCD_SOURCE_CHECKED_READ_EVENT_H_
#include "evaluation_plan.h"
namespace abcd::source {
// A checked capture is one native read with two original members. Its value
// belongs to the load, but its abrupt completion belongs to the named guard.
// This view requires independently verified guarded_reads and cell bindings;
// it never changes raw IR or grants an initialized-cell premise.
inline analysis::OpId EvaluationThrowSite(const EvaluationPlan& plan, analysis::OpId operation) {
  return GuardedInput(plan, operation) ? *GuardedPeer(plan, operation) : operation;
}
inline bool EvaluationMayThrow(const analysis::ValueProgram& graph, const EvaluationPlan& plan,
    analysis::OpId operation) {
  return graph.ops[EvaluationThrowSite(plan, operation).value].exceptional ==
      analysis::ExceptionalCompletion::kMayThrow;
}
}  // namespace abcd::source
#endif
