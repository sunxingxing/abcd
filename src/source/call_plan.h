#ifndef ABCD_SOURCE_CALL_PLAN_H_
#define ABCD_SOURCE_CALL_PLAN_H_

#include "evaluation_plan.h"
#include "abcd/source/call_reference.h"

namespace abcd::source {
// Native binding obligation shared by scheduling and fresh call verification.
// Protocol-owned calls and undefined-receiver apply remain NoCall/value calls;
// property identity, receiver stability and total order still need their proofs.
[[nodiscard]] bool NeedsMemberReference(const analysis::ValueProgram&,
    const EvaluationPlan&, analysis::OpId call);
[[nodiscard]] std::optional<analysis::OpId> MemberProperty(const analysis::ValueProgram&,
    const EvaluationPlan&, analysis::OpId);
[[nodiscard]] bool MemberReferenceInline(const analysis::ValueProgram&, const EvaluationPlan&,
    analysis::OpId call, analysis::OpId property);
struct NoCall {};
enum class EvalMode : std::uint8_t { kIndirect };
struct ValueCall { EvalMode eval = EvalMode::kIndirect; std::optional<analysis::OpId> array; };
struct MemberCall { analysis::OpId property; analysis::UseId receiver; std::optional<analysis::OpId> array; };
// Absent is the ordinary explicit argument range. Present identifies the exact
// private array consumed by NEWOBJAPPLY; shared arrays are never expanded.
struct ConstructCall {
  std::optional<analysis::OpId> array;
  std::optional<analysis::OpId> constructor;
};
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
  kResourceLimit, kWorkLimit, kArrayArguments, kConstructorIdentity
};
struct CallPlanFailure {
  CallPlanError kind;
  std::variant<std::monostate, CallReferenceError, CallSpreadError> cause;
  std::optional<abc::CodeOffset> pc, working_pc;
  std::optional<CallReferenceEvidence> reference;
  CallPlanFailure(CallPlanError value) : kind(value) {}  // NOLINT: implicit error kind.
  bool operator==(CallPlanError value) const { return kind == value; }
};
struct CallArgumentUses {
  std::vector<std::size_t> values;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// One canonical census for apply arrays. Checked literal receiver definitions
// and alias copies are not observations; snapshots/escapes remain observations.
// Requires fresh evaluation verification, supplied by call/tree verification.
[[nodiscard]] std::variant<CallArgumentUses, CallPlanError> IndexCallArgumentUses(
    const analysis::ValueProgram&, const EvaluationPlan&, analysis::SemanticBudget = {});
// Normal, verified IR only. Member syntax consumes an inlined property read
// with exactly the same stable receiver value. No .call lookup or staged IIFE.
// Value calls must print indirect-call syntax even if their identifier is eval.
[[nodiscard]] std::variant<CallPlan, CallPlanFailure> BuildCallPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    analysis::SemanticBudget budget = {}, const ForOfEvaluationInput* iteration = nullptr);
// Checks supplied binding records, not a regenerated plan. Does not replace
// final AST operand/receiver and generated-order correspondence checks.
[[nodiscard]] std::variant<std::uint64_t, CallPlanFailure> VerifyCallPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    const CallPlan& plan, analysis::SemanticBudget budget = {}, const ForOfEvaluationInput* iteration = nullptr);
// Same call/reference checks with verified phased evaluation. In particular,
// an explicit receiver does not permit a .call lookup or getter re-evaluation.
[[nodiscard]] std::variant<CallPlan, CallPlanFailure> BuildPhasedCallPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    analysis::SemanticBudget budget = {}, const ForOfEvaluationInput* iteration = nullptr);
[[nodiscard]] std::variant<std::uint64_t, CallPlanFailure> VerifyPhasedCallPlan(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    const CallPlan& plan, analysis::SemanticBudget budget = {}, const ForOfEvaluationInput* iteration = nullptr);
}  // namespace abcd::source
#endif
