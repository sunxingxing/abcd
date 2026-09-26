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
  receipt.work_revision = graph->revision.value;
  bool needs_environments = false;
  if (graph->ops.size() > budget.work)
    return fail(RouteFailureClass::kWorkLimit, ValueRouteError::kTransformStage);
  for (const auto& op : graph->ops) {
    if (op.life != analysis::ValueLife::kLive) continue;
    needs_environments |= std::holds_alternative<analysis::ValueLexicalLoad>(op.operation) ||
        std::holds_alternative<analysis::ValueLexicalStore>(op.operation) ||
        std::holds_alternative<analysis::ValueEnvironmentPush>(op.operation) ||
        std::holds_alternative<analysis::ValueEnvironmentPop>(op.operation) ||
        std::holds_alternative<analysis::ValueCurrentEnvironment>(op.operation) ||
        std::holds_alternative<analysis::ValueFunctionCreation>(op.operation);
  }
  if (needs_environments) {
    // Input/proof remain alive. Admit the retained working graph before adding
    // topology scratch/result; no borrowed input environment survives this stage.
    if (!admit(1, sizeof(*graph)) ||
        !admit(graph->blocks.capacity(), sizeof(analysis::WorkBlock)) ||
        !admit(graph->edges.capacity(), sizeof(analysis::WorkEdge)) ||
        !admit(graph->ops.capacity(), sizeof(analysis::ValueOp)) ||
        !admit(graph->values.capacity(), sizeof(analysis::ValueDefinition)) ||
        !admit(graph->uses.capacity(), sizeof(analysis::OperandUse)) ||
        !admit(graph->origins.capacity(), sizeof(analysis::ValueOrigin)) ||
        !admit(graph->exceptional_exits.capacity(), sizeof(analysis::ValueExceptionalExit)) ||
        !admit(graph->input.sites.capacity(), sizeof(std::optional<analysis::OpId>)) ||
        !admit(graph->input.values.capacity(), sizeof(analysis::ValueId)))
      return fail(RouteFailureClass::kMemoryLimit, ValueRouteError::kTransformStage);
    auto environments = analysis::BuildValueEnvironments(*graph, {remaining, budget.work - graph->ops.size()});
    if (const auto* error = std::get_if<analysis::ValueEnvironmentError>(&environments)) {
      const auto kind = *error == analysis::ValueEnvironmentError::kResourceLimit ? RouteFailureClass::kMemoryLimit :
          *error == analysis::ValueEnvironmentError::kWorkLimit ? RouteFailureClass::kWorkLimit : RouteFailureClass::kUnproved;
      return fail(kind, ValueRouteError::kTransformStage);
    }
    result.environments = std::get<analysis::ValueEnvironments>(std::move(environments));
  }
  receipt.stage = SourceStage::kValuesVerified;
  receipt.coverage = Coverage::kIncomplete;
  result.graph = std::move(*graph);
  return result;
}
}  // namespace abcd::source
