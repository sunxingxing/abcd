#include "function_values.h"
#include "function_captures.h"

#include "abcd/analysis/value_import.h"
#include "abcd/analysis/value_simplify.h"

namespace abcd::source {
std::variant<FunctionValues, FunctionValueFailure> BuildFunctionValues(
    const FunctionInput& owner, const abc::MethodBody& body,
    const abc::FrameLayout& frame, analysis::SemanticBudget budget) {
  const auto& input = owner.Input();
  const auto& proof = owner.Proof();
  auto stage = SourceStage::kInputVerified;
  std::optional<std::uint64_t> revision;
  const auto fail = [&](RouteFailureClass kind, ValueRouteError reason,
                        std::optional<abc::CodeOffset> pc = {}) {
    return FunctionValueFailure{stage, {kind, reason, pc}, revision};
  };
  auto imported = analysis::ImportNormalValues(body, input, proof, frame, budget);
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
  // Import admitted both use-chain and operand-view scratch with disjoint
  // lifetimes. Preserve its fixed view minimum even for tiny methods.
  const auto verified = analysis::VerifyImportedValues(*graph, input, proof, frame,
      {analysis::ImportedValueScratch(graph->uses.size()), budget.work});
  if (verified)
    return fail(verified->kind == analysis::ValueImportError::kWorkLimit
                    ? RouteFailureClass::kWorkLimit :
                verified->kind == analysis::ValueImportError::kResourceLimit
                    ? RouteFailureClass::kMemoryLimit : RouteFailureClass::kInvariantViolation,
                ValueRouteError::kImportStage, verified->pc);
  stage = SourceStage::kValuesImported;
  revision = graph->revision.value;
  const auto simplified = analysis::SimplifyValues(*graph, graph->revision, budget);
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
  revision = graph->revision.value;
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
  std::optional<analysis::ValueEnvironments> environments;
  std::optional<CellPlan> cells;
  if (needs_environments) {
    auto remaining = budget.bytes;
    const auto admit = [&](std::size_t count, std::size_t width) {
      if (count > remaining / width) return false;
      remaining -= count * width;
      return true;
    };
    // The caller admitted input/proof. Keep the working graph alive alongside
    // environment scratch/result; never borrow input-local EnvironmentIds.
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
    auto built = analysis::BuildValueEnvironments(*graph, {remaining, budget.work - graph->ops.size()});
    if (const auto* error = std::get_if<analysis::ValueEnvironmentError>(&built)) {
      const auto kind = *error == analysis::ValueEnvironmentError::kResourceLimit ? RouteFailureClass::kMemoryLimit :
          *error == analysis::ValueEnvironmentError::kWorkLimit ? RouteFailureClass::kWorkLimit : RouteFailureClass::kUnproved;
      return fail(kind, ValueRouteError::kTransformStage);
    }
    environments = std::get<analysis::ValueEnvironments>(std::move(built));
    if (!admit(environments->allocated_bytes, 1))
      return fail(RouteFailureClass::kMemoryLimit, ValueRouteError::kTransformStage);
    const auto work = budget.work - graph->ops.size();
    if (environments->work > work)
      return fail(RouteFailureClass::kWorkLimit, ValueRouteError::kTransformStage);
    const auto captures = VerifyFunctionCaptures(owner, *graph, *environments,
        {remaining, work - environments->work});
    if (const auto* error = std::get_if<FunctionCaptureError>(&captures)) {
      auto kind = RouteFailureClass::kInvariantViolation;
      switch (*error) {
        case FunctionCaptureError::kResourceLimit: kind = RouteFailureClass::kMemoryLimit; break;
        case FunctionCaptureError::kWorkLimit: kind = RouteFailureClass::kWorkLimit; break;
        case FunctionCaptureError::kEnvironment:
        case FunctionCaptureError::kOrigin:
        case FunctionCaptureError::kCoverage:
        case FunctionCaptureError::kBinding: break;
      }
      return fail(kind, ValueRouteError::kTransformStage);
    }
    if (owner.Mode() == InputCaptureMode::kConditional) {
      auto planned = BuildCellPlan(*graph, *environments,
          {remaining, work - environments->work - std::get<std::uint64_t>(captures)});
      if (const auto* error = std::get_if<CellError>(&planned)) {
        const auto kind = *error == CellError::kResourceLimit ? RouteFailureClass::kMemoryLimit :
            *error == CellError::kWorkLimit ? RouteFailureClass::kWorkLimit : RouteFailureClass::kInvariantViolation;
        return fail(kind, ValueRouteError::kTransformStage);
      }
      cells = std::get<CellPlan>(std::move(planned));
    }
  } else if (!owner.Captures().empty()) {
    return fail(RouteFailureClass::kInvariantViolation, ValueRouteError::kTransformStage);
  }
  return FunctionValues{std::move(*graph), std::move(environments), owner.Mode(),
      owner.Identity(), owner.Method(), owner.Record(), std::move(cells)};
}
}  // namespace abcd::source
