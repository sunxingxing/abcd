#ifndef ABCD_SOURCE_ASYNC_FAILURE_H_
#define ABCD_SOURCE_ASYNC_FAILURE_H_
#include "abcd/source/function_emit_error.h"
#include "abcd/source/tree_failure.h"
#include "abcd/analysis/await_region.h"
#include "abcd/analysis/await_catch.h"
#include "abcd/analysis/await_catch_flow.h"
#include <memory>
namespace abcd::source {
// Diagnostic checks only; no new normalization admission. Original PCs remain
// original while no working body exists. Missing sites are not encoded as PC0.
enum class AsyncCompletionCheck : std::uint8_t {
  kInput, kPreludeEffects, kPreludeOperation, kEnter, kPromiseStore,
  kPromisePreludeUse, kAccumulatorEffect, kAccumulatorRead, kAccumulatorControl,
  kAccumulatorFallthrough, kAccumulatorTarget, kCatchStore, kCatchRegister,
  kReject, kRejectReturn, kResolve, kResolveReturn, kPromiseIdentity,
  kCompletionCatch, kAwaitRegion, kCatchInput, kAwaitState, kAwaitBounds,
  kResolveIdentity, kResolveSuccessor, kBodyControl, kReturnProtocol,
  kBranchBounds, kPromiseUse, kAwaitCoverage, kBranchRebase, kHandlerInput,
  kHandlerRange, kTryBoundary, kHandlerBoundary, kBudget
};
struct AsyncCompletionEvidence {
  AsyncCompletionCheck check = AsyncCompletionCheck::kInput;
  TreeLocation location;
  std::optional<abc::CodeOffset> related_pc;
  std::optional<analysis::AwaitCatchObservation> catch_input;
  // Separately charged immutable query report; the failure witness stays fixed-size.
  std::shared_ptr<const analysis::AwaitCatchFlowDiagnostic> catch_flow;
  bool operator==(const AsyncCompletionEvidence& e) const {
    return check == e.check && location == e.location && related_pc == e.related_pc &&
        catch_input == e.catch_input && ((!catch_flow && !e.catch_flow) ||
        (catch_flow && e.catch_flow && *catch_flow == *e.catch_flow));
  }
};
struct AsyncCompletionFailure {
  FunctionEmitError kind;
  AsyncCompletionEvidence async;
  std::variant<std::monostate, analysis::AwaitRegionError, analysis::AwaitCatchError> cause;
  AsyncCompletionFailure(FunctionEmitError error) : kind(error) {
    if (error == FunctionEmitError::kResourceLimit) async.check = AsyncCompletionCheck::kBudget;
  }
  AsyncCompletionFailure(AsyncCompletionCheck check, std::optional<abc::CodeOffset> pc = {})
      : kind(FunctionEmitError::kKind) { async.check = check; async.location.pc = pc; }
};
} // namespace abcd::source
#endif
