#ifndef ABCD_SOURCE_CALL_PLAN_H_
#define ABCD_SOURCE_CALL_PLAN_H_

#include "evaluation_plan.h"

namespace abcd::source {
struct NoCall {};
enum class EvalMode : std::uint8_t { kIndirect };
struct ValueCall { EvalMode eval = EvalMode::kIndirect; };
struct MemberCall { analysis::OpId property; analysis::UseId receiver; };
struct ConstructCall {};
using CallBinding = std::variant<NoCall, ValueCall, MemberCall, ConstructCall>;
struct CallPlan {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<CallBinding> operations;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
enum class CallPlanError : std::uint8_t {
  kRevision, kEvaluation, kCallee, kReceiver, kReference, kBinding,
  kResourceLimit, kWorkLimit
};
// Normal, verified IR only. Member syntax consumes an inlined property read
// with exactly the same stable receiver value. No .call lookup or staged IIFE.
// Value calls must print indirect-call syntax even if their identifier is eval.
[[nodiscard]] std::variant<CallPlan, CallPlanError> BuildCallPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    analysis::SemanticBudget budget = {});
// Checks supplied binding records, not a regenerated plan. Does not replace
// final AST operand/receiver and generated-order correspondence checks.
[[nodiscard]] std::variant<std::uint64_t, CallPlanError> VerifyCallPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    const CallPlan& plan, analysis::SemanticBudget budget = {});
// Same call/reference checks with verified phased evaluation. In particular,
// an explicit receiver does not permit a .call lookup or getter re-evaluation.
[[nodiscard]] std::variant<CallPlan, CallPlanError> BuildPhasedCallPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    analysis::SemanticBudget budget = {});
[[nodiscard]] std::variant<std::uint64_t, CallPlanError> VerifyPhasedCallPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    const CallPlan& plan, analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
