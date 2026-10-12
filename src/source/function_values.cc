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
  // Same failure with its typed leaf cause (S2).
  const auto fail_with = [&]<class E>(RouteFailureClass kind, ValueRouteError reason,
                                      E cause, std::optional<abc::CodeOffset> pc = {}) {
    auto failure = fail(kind, reason, pc);
    AppendFailureCause(failure.cause.causes, cause);
    return failure;
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
    return fail_with(kind, ValueRouteError::kImportStage, error.kind, error.pc);
  }
  // Import admitted normal block-map storage and the use/view scratch. The
  // independent consumer needs original block cardinality after compaction.
  const auto verified = analysis::VerifyImportedValues(*graph, input, proof, frame,
      {analysis::ImportedValueScratch(graph->uses.size(), input.flow.blocks.size()), budget.work});
  if (verified)
    return fail_with(verified->kind == analysis::ValueImportError::kWorkLimit
                         ? RouteFailureClass::kWorkLimit :
                     verified->kind == analysis::ValueImportError::kResourceLimit
                         ? RouteFailureClass::kMemoryLimit : RouteFailureClass::kInvariantViolation,
                     ValueRouteError::kImportStage, verified->kind, verified->pc);
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
    return fail_with(kind, ValueRouteError::kTransformStage, *error);
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
        std::holds_alternative<analysis::ValueFunctionCreation>(op.operation) ||
        std::holds_alternative<analysis::ValueClassCreation>(op.operation);
  }
  std::optional<analysis::ValueEnvironments> environments;
  std::optional<CellPlan> cells;
  auto remaining = budget.bytes;
  const auto admit = [&](std::size_t count, std::size_t width) {
    if (count > remaining / width) return false;
    remaining -= count * width;
    return true;
  };
  // The caller admitted input/proof. Keep the working graph alive alongside
  // module/environment scratch and results; IDs from original proof tables
  // must be explicitly rebased onto this working graph.
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
  auto guarded = BuildModuleGuards(owner, *graph, {remaining, budget.work - graph->ops.size()});
  if (const auto* error = std::get_if<ModuleGuardError>(&guarded)) {
    const auto kind = *error == ModuleGuardError::kResourceLimit ? RouteFailureClass::kMemoryLimit :
        *error == ModuleGuardError::kWorkLimit ? RouteFailureClass::kWorkLimit :
        *error == ModuleGuardError::kUnsupportedProof ? RouteFailureClass::kUnsupported : RouteFailureClass::kInvariantViolation;
    return fail_with(kind, ValueRouteError::kTransformStage, *error);
  }
  auto modules = std::get<ModuleGuards>(std::move(guarded));
  if (!admit(modules.allocated_bytes, 1))
    return fail(RouteFailureClass::kMemoryLimit, ValueRouteError::kTransformStage);
  auto work = budget.work - graph->ops.size() - modules.work;
  auto heritage = analysis::AnalyzeValueHeritage(*graph, {remaining, work});
  if (const auto* error = std::get_if<analysis::ValueHeritageError>(&heritage)) {
    const auto kind = *error == analysis::ValueHeritageError::kResourceLimit ? RouteFailureClass::kMemoryLimit :
        *error == analysis::ValueHeritageError::kWorkLimit ? RouteFailureClass::kWorkLimit :
        *error == analysis::ValueHeritageError::kDomain ? RouteFailureClass::kUnsupported : RouteFailureClass::kInvariantViolation;
    return fail_with(kind, ValueRouteError::kTransformStage, *error);
  }
  auto classes = std::get<analysis::ValueHeritages>(std::move(heritage));
  if (!admit(classes.allocated_bytes, 1)) return fail(RouteFailureClass::kMemoryLimit, ValueRouteError::kTransformStage);
  work -= classes.work;
  auto initializer_calls = FindClassInitializerCalls(*graph, {remaining, work});
  if (const auto* error = std::get_if<ClassInitializerError>(&initializer_calls)) {
    const auto kind = *error == ClassInitializerError::kResourceLimit ? RouteFailureClass::kMemoryLimit :
        *error == ClassInitializerError::kWorkLimit ? RouteFailureClass::kWorkLimit : RouteFailureClass::kInvariantViolation;
    return fail_with(kind, ValueRouteError::kTransformStage, *error);
  }
  auto initializers = std::get<ClassInitializerCalls>(std::move(initializer_calls));
  if (!admit(initializers.allocated_bytes, 1)) return fail(RouteFailureClass::kMemoryLimit, ValueRouteError::kTransformStage);
  work -= initializers.work;
  auto member_sites = FindClassDefinitions(*graph, {remaining, work});
  if (const auto* error = std::get_if<ClassDefinitionError>(&member_sites)) {
    const auto kind = *error == ClassDefinitionError::kResourceLimit ? RouteFailureClass::kMemoryLimit :
        *error == ClassDefinitionError::kWorkLimit ? RouteFailureClass::kWorkLimit : RouteFailureClass::kInvariantViolation;
    return fail_with(kind, ValueRouteError::kTransformStage, *error);
  }
  auto definitions = std::get<ClassDefinitionSites>(std::move(member_sites));
  if (!admit(definitions.allocated_bytes, 1)) return fail(RouteFailureClass::kMemoryLimit, ValueRouteError::kTransformStage);
  work -= definitions.work;
  if (needs_environments) {
    auto built = analysis::BuildValueEnvironments(*graph, {remaining, work});
    if (const auto* error = std::get_if<analysis::ValueEnvironmentError>(&built)) {
      const auto kind = *error == analysis::ValueEnvironmentError::kResourceLimit ? RouteFailureClass::kMemoryLimit :
          *error == analysis::ValueEnvironmentError::kWorkLimit ? RouteFailureClass::kWorkLimit : RouteFailureClass::kUnproved;
      return fail_with(kind, ValueRouteError::kTransformStage, *error);
    }
    environments = std::get<analysis::ValueEnvironments>(std::move(built));
    if (!admit(environments->allocated_bytes, 1))
      return fail(RouteFailureClass::kMemoryLimit, ValueRouteError::kTransformStage);
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
      return fail_with(kind, ValueRouteError::kTransformStage, *error);
    }
    if (owner.Mode() == InputCaptureMode::kConditional) {
      auto planned = BuildCellPlan(*graph, *environments,
          {remaining, work - environments->work - std::get<std::uint64_t>(captures)});
      if (const auto* error = std::get_if<CellError>(&planned)) {
        const auto kind = *error == CellError::kResourceLimit ? RouteFailureClass::kMemoryLimit :
            *error == CellError::kWorkLimit ? RouteFailureClass::kWorkLimit : RouteFailureClass::kInvariantViolation;
        return fail_with(kind, ValueRouteError::kTransformStage, *error);
      }
      cells = std::get<CellPlan>(std::move(planned));
    }
  } else if (!owner.Captures().empty()) {
    return fail(RouteFailureClass::kInvariantViolation, ValueRouteError::kTransformStage);
  }
  return FunctionValues{std::move(*graph), std::move(environments), owner.Mode(),
      owner.Identity(), owner.Method(), owner.Record(), std::move(cells), std::move(modules), std::move(classes), std::move(initializers), std::move(definitions)};
}
}  // namespace abcd::source
