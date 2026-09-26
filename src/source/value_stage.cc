#include "abcd/source/value_stage.h"
#include "function_values.h"

namespace abcd::source {
ValueMethod BuildValueStage(RouteInputIdentity identity, abc::EntityOffset method,
    abc::EntityOffset record, const abc::MethodBody& body,
    const abc::FrameLayout& frame, analysis::SemanticBudget budget) {
  ValueMethod result;
  auto& receipt = result.receipt;
  receipt.identity = identity;
  receipt.method = method;
  receipt.record = record;
  receipt.route = SourceRoute::kValueTree;
  receipt.stage = SourceStage::kDecoded;
  receipt.coverage = Coverage::kUnsupported;
  const auto fail = [&](RouteFailureClass kind, ValueRouteError reason,
                        std::optional<abc::CodeOffset> pc = {}) {
    receipt.failure = RouteFailure{kind, reason, pc};
    return std::move(result);
  };
  auto remaining = budget.bytes;
  const auto admit = [&](std::size_t count, std::size_t width) {
    if (count > remaining / width) return false;
    remaining -= count * width;
    return true;
  };
  if (!admit(body.instructions.capacity(), sizeof(abc::BodyInstruction)) ||
      !admit(body.tries.capacity(), sizeof(abc::TryRegion)) ||
      !admit(body.handlers.capacity(), sizeof(abc::CatchHandler)))
    return fail(RouteFailureClass::kMemoryLimit, ValueRouteError::kInputStage);
  auto prepared = FunctionInput::Build(identity, method, record, body, frame,
      InputCaptureMode::kClosed, {remaining, budget.work});
  const auto* owner = std::get_if<FunctionInput>(&prepared);
  if (!owner) {
    const auto& cause = std::get<FunctionInputFailure>(prepared).cause;
    if (const auto* error = std::get_if<analysis::SemanticFailure>(&cause))
      return fail(error->kind == analysis::SemanticError::kResourceLimit
          ? RouteFailureClass::kMemoryLimit : RouteFailureClass::kUnsupported, ValueRouteError::kInputStage, error->pc);
    if (const auto* error = std::get_if<analysis::SemanticProofFailure>(&cause))
      return fail(error->kind == analysis::SemanticProofError::kResourceLimit
          ? RouteFailureClass::kMemoryLimit : RouteFailureClass::kUnproved, ValueRouteError::kInputStage, error->pc);
    switch (std::get<FunctionInputError>(cause)) {
      case FunctionInputError::kResourceLimit: return fail(RouteFailureClass::kMemoryLimit, ValueRouteError::kInputStage);
      case FunctionInputError::kWorkLimit: return fail(RouteFailureClass::kWorkLimit, ValueRouteError::kInputStage);
      case FunctionInputError::kMode:
      case FunctionInputError::kCapture: return fail(RouteFailureClass::kInvariantViolation, ValueRouteError::kInputStage);
    }
    return fail(RouteFailureClass::kInvariantViolation, ValueRouteError::kInputStage);
  }
  receipt.stage = SourceStage::kInputVerified;
  // The envelope charge includes its input method, proof and capture ledgers.
  if (!admit(owner->AllocatedBytes(), 1))
    return fail(RouteFailureClass::kMemoryLimit, ValueRouteError::kImportStage);
  auto built = BuildFunctionValues(*owner, body, frame, {remaining, budget.work});
  auto* values = std::get_if<FunctionValues>(&built);
  if (!values) {
    const auto& error = std::get<FunctionValueFailure>(built);
    receipt.stage = error.stage;
    receipt.work_revision = error.revision;
    return fail(error.cause.failure_class, error.cause.reason, error.cause.pc);
  }
  if (values->capture_mode != InputCaptureMode::kClosed)
    return fail(RouteFailureClass::kInvariantViolation, ValueRouteError::kImportStage);
  receipt.stage = SourceStage::kValuesVerified;
  receipt.work_revision = values->graph.revision.value;
  receipt.coverage = Coverage::kIncomplete;
  result.graph = std::move(values->graph);
  result.environments = std::move(values->environments);
  return result;
}
}  // namespace abcd::source
