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
struct CleanupTransferPair { ControlId normal, exceptional; };
enum class CleanupPlacement : std::uint8_t { kAfterTry, kBodyTail };
struct FinalizerRegion {
  analysis::TryId region;
  PendingCompletion normal;
  PendingCompletion exceptional;
  // Fallthrough/loop-transfer cases identify the last paired cleanup operation;
  // value completions identify their return/throw. A bare loop transfer has
  // no opcode anchor; its actual control IDs live in transfer.
  std::optional<analysis::OpId> normal_exit;
  std::optional<analysis::OpId> exceptional_exit;
  // Original projected paths, including alias/copy boundaries. Pairing does
  // not erase operations, move edge copies, or publish a rewritten ValueProgram.
  std::vector<ControlId> normal_path;
  std::vector<ControlId> exceptional_path;
  std::vector<CleanupPair> cleanup;
  // Both cleanup paths end with the same overriding completion. Value terminals
  // are included in cleanup; loop terminals have separate control IDs below.
  std::optional<PendingCompletion> replacement;
  // A typed synthetic loop transfer is not an opcode. Keep its separate IDs.
  std::optional<CleanupTransferPair> transfer{};
  CleanupPlacement placement = CleanupPlacement::kAfterTry;
  // Shared pending-Hole protocol: cleanup lies between the normally completing
  // try/catch and this rethrow branch. Keep the payload test/rethrow outside;
  // the finalizer moves existing roots, without duplicated cleanup or new SSA.
  std::optional<analysis::OpId> shared_guard;
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
