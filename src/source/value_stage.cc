#include "abcd/source/value_stage.h"
#include "abcd/analysis/value_simplify.h"

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
  auto lifted = analysis::BuildSemanticMethod(body, identity.profile,
                                             frame.registers, {remaining, budget.work});
  auto* input = std::get_if<analysis::SemanticMethod>(&lifted);
  if (!input) {
    const auto& error = std::get<analysis::SemanticFailure>(lifted);
    return fail(error.kind == analysis::SemanticError::kResourceLimit
                    ? RouteFailureClass::kMemoryLimit : RouteFailureClass::kUnsupported,
                ValueRouteError::kInputStage, error.pc);
  }
  auto checked = analysis::VerifySemanticSource(body, *input, frame.registers,
      frame.locals, {remaining, budget.work}, analysis::CapturedValues::kReject,
      {}, analysis::ClosureWrites::kUnknown, {}, NormalCallResults(identity.contract));
  const auto* proof = std::get_if<analysis::SemanticProof>(&checked);
  if (!proof) {
    const auto& error = std::get<analysis::SemanticProofFailure>(checked);
    return fail(error.kind == analysis::SemanticProofError::kResourceLimit
                    ? RouteFailureClass::kMemoryLimit : RouteFailureClass::kUnproved,
                ValueRouteError::kInputStage, error.pc);
  }
  receipt.stage = SourceStage::kInputVerified;
  // SemanticProof::allocated_bytes already includes the input SemanticMethod.
  if (!admit(proof->allocated_bytes, 1))
    return fail(RouteFailureClass::kMemoryLimit, ValueRouteError::kImportStage);
  auto imported = analysis::ImportNormalValues(body, *input, *proof, frame,
                                               {remaining, budget.work});
  auto* graph = std::get_if<analysis::ValueProgram>(&imported);
  if (!graph) {
    const auto& error = std::get<analysis::ValueImportFailure>(imported);
    auto kind = RouteFailureClass::kInvariantViolation;
    switch (error.kind) {
      case analysis::ValueImportError::kUnsupportedOperation:
      case analysis::ValueImportError::kExceptionFlow:
      case analysis::ValueImportError::kUnreachableFlow:
      case analysis::ValueImportError::kResumeFlow: kind = RouteFailureClass::kUnsupported; break;
      case analysis::ValueImportError::kWorkLimit: kind = RouteFailureClass::kWorkLimit; break;
      case analysis::ValueImportError::kResourceLimit: kind = RouteFailureClass::kMemoryLimit; break;
      case analysis::ValueImportError::kInputMismatch:
      case analysis::ValueImportError::kOperandMismatch:
      case analysis::ValueImportError::kInvalidPhi:
      case analysis::ValueImportError::kInvalidGraph: break;
    }
    return fail(kind, ValueRouteError::kImportStage, error.pc);
  }
  // Operand views and use-chain scratch have disjoint lifetimes. The import
  // budget checked both; preserve the fixed view minimum for small methods.
  const auto verified = analysis::VerifyImportedValues(*graph, *input, *proof, frame,
      {analysis::ImportedValueScratch(graph->uses.size()), budget.work});
  if (verified)
    return fail(verified->kind == analysis::ValueImportError::kWorkLimit
                    ? RouteFailureClass::kWorkLimit :
                verified->kind == analysis::ValueImportError::kResourceLimit
                    ? RouteFailureClass::kMemoryLimit : RouteFailureClass::kInvariantViolation,
                ValueRouteError::kImportStage, verified->pc);
  receipt.stage = SourceStage::kValuesImported;
  receipt.work_revision = graph->revision.value;
  const auto simplified = analysis::SimplifyValues(*graph, graph->revision, {remaining, budget.work});
  if (const auto* error = std::get_if<analysis::ValueSimplifyError>(&simplified)) {
    auto kind = RouteFailureClass::kInvariantViolation;
    switch (*error) {
      case analysis::ValueSimplifyError::kWorkLimit: kind = RouteFailureClass::kWorkLimit; break;
      case analysis::ValueSimplifyError::kResourceLimit: kind = RouteFailureClass::kMemoryLimit; break;
      case analysis::ValueSimplifyError::kUnsupportedFlow: kind = RouteFailureClass::kUnsupported; break;
      case analysis::ValueSimplifyError::kStaleRevision:
      case analysis::ValueSimplifyError::kRevisionLimit:
      case analysis::ValueSimplifyError::kInvalidGraph:
      case analysis::ValueSimplifyError::kCopyCycle: break;
    }
    return fail(kind, ValueRouteError::kTransformStage);
  }
  receipt.stage = SourceStage::kValuesVerified;
  receipt.coverage = Coverage::kIncomplete;
  receipt.work_revision = graph->revision.value;
  result.graph = std::move(*graph);
  return result;
}
}  // namespace abcd::source
