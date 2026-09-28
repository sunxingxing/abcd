#ifndef ABCD_SOURCE_COMPLETION_PLAN_H_
#define ABCD_SOURCE_COMPLETION_PLAN_H_

#include "evaluation_plan.h"
#include "exception_control.h"

namespace abcd::source {
// JS completion, not the opcode's ExceptionalCompletion effect classification.
// Absence in Normal means empty completion; it is not the JS undefined value.
struct NormalCompletion { std::optional<analysis::ValueOperand> value; };
struct ReturnCompletion { analysis::ValueOperand value; };
struct ThrowCompletion { analysis::ValueOperand value; };
struct BreakCompletion { LoopId target; };
struct ContinueCompletion { LoopId target; };
using PendingCompletion = std::variant<NormalCompletion, ReturnCompletion,
    ThrowCompletion, BreakCompletion, ContinueCompletion>;
struct CleanupPair { analysis::OpId normal, exceptional; };
struct FinalizerRegion {
  analysis::TryId region;
  PendingCompletion normal;
  PendingCompletion exceptional;
  analysis::OpId normal_exit;
  analysis::OpId exceptional_exit;
  // Original projected paths, including alias/copy boundaries. Pairing does
  // not erase operations, move edge copies, or publish a rewritten ValueProgram.
  std::vector<ControlId> normal_path;
  std::vector<ControlId> exceptional_path;
  std::vector<CleanupPair> cleanup;
  // Present only when both cleanup paths end with the same overriding return
  // or throw. That terminal pair is part of cleanup, including its evaluation.
  std::optional<PendingCompletion> replacement;
};
struct CompletionPlan {
  analysis::IrRevision revision;
  analysis::CfgRevision cfg_revision;
  std::vector<FinalizerRegion> finalizers;
};
enum class CompletionError : std::uint8_t { kInput, kWorkLimit, kResourceLimit };
// Requires the checked phased graph/control/evaluation. Matches return/rethrow
// cleanup corridors by typed operations, SSA operands and exceptional dispatch.
// Unsupported completion shapes remain unchanged, never guessed finalizers.
// No string/PC-nearness matching. O(V+E+O+U+P), where P is the examined disjoint
// corridor/body topology; matching internal SSA results uses indexed pairing.
[[nodiscard]] std::variant<CompletionPlan, CompletionError> BuildCompletionPlan(
    const analysis::ValueProgram& graph, const ExceptionPlan& exceptions,
    const ExceptionControl& control, const EvaluationPlan& evaluation,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
