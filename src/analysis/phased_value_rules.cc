#include "phased_value_rules.h"

namespace abcd::analysis::detail {
FlowEdgeKind PhaseEdgeKind(ExceptionArcKind kind) {
  switch (kind) {
    case ExceptionArcKind::kNormal: case ExceptionArcKind::kFallthrough:
      return FlowEdgeKind::kFallthrough;
    case ExceptionArcKind::kTaken: return FlowEdgeKind::kTaken;
    case ExceptionArcKind::kException: return FlowEdgeKind::kException;
  }
  throw ValueImportFailure{PhaseError::kInputMismatch, {}};
}
OperandView PhaseOperands(const SemanticInstruction& instruction, PhaseQuota& quota) {
  auto built = OperandView::Build(instruction, quota.Remaining());
  if (auto* view = std::get_if<OperandView>(&built)) {
    quota.Work(view->Work());
    return std::move(*view);
  }
  auto error = PhaseError::kOperandMismatch;
  switch (std::get<OperandViewError>(built)) {
    case OperandViewError::kUnsupported: error = PhaseError::kUnsupportedOperation; break;
    case OperandViewError::kMismatch: break;
    case OperandViewError::kResourceLimit: error = PhaseError::kResourceLimit; break;
    case OperandViewError::kWorkLimit: error = PhaseError::kWorkLimit; break;
  }
  throw ValueImportFailure{error, instruction.pc};
}
void CheckPhaseInput(const PhasedValueInput& input, PhaseQuota& quota) {
  const auto& frame = input.frame;
  RequirePhase(frame.locals <= frame.registers, PhaseError::kInputMismatch);
  const auto built = abc::BuildFrameLayout(frame.locals, frame.registers - frame.locals,
      {frame.this_value.has_value(), frame.new_target.has_value(), frame.extra_arguments,
       frame.function.has_value()});
  const auto* layout = std::get_if<abc::FrameLayout>(&built);
  RequirePhase(layout && *layout == frame && (!input.flow.frame || *input.flow.frame == frame),
      PhaseError::kInputMismatch);
  const auto checked = VerifyExceptionInputs(input.flow, input.ssa, input.environments,
      input.values, quota.Remaining().work,
      input.proof.arguments ? &*input.proof.arguments : nullptr,
      ExceptionInputDomain::kHoleTransport);
  if (const auto* failure = std::get_if<ExceptionInputFailure>(&checked))
    throw ValueImportFailure{failure->kind == ExceptionInputError::kResourceLimit
        ? PhaseError::kWorkLimit : PhaseError::kInputMismatch, failure->pc};
  const auto& proof = std::get<ExceptionInputProof>(checked);
  quota.Work(proof.work_units);
  RequirePhase(proof.arguments == input.proof.arguments && proof.reads == input.proof.reads &&
      proof.work_units == input.proof.work_units, PhaseError::kInputMismatch);
  for (std::size_t i = 0; i < input.environments.before.size(); ++i) {
    quota.Work();
    if (input.environments.before[i]) continue;
    const auto& node = input.flow.nodes[i];
    const auto& instruction = input.flow.instructions[node.instruction.value];
    RequirePhase(node.phase == ExceptionPhase::kCommit &&
        std::holds_alternative<ThrowOperation>(instruction.operation) &&
        instruction.effects.exceptional == ExceptionalCompletion::kAlwaysThrow,
        PhaseError::kUnreachableFlow);
  }
}
ValueOperand PhaseLiteral(const ScalarValue& value) {
  return std::visit([](const auto& literal) -> ValueOperand {
    using T = std::decay_t<decltype(literal)>;
    if constexpr (std::is_same_v<T, SpecialValue> || std::is_same_v<T, IntegerLiteral> ||
                  std::is_same_v<T, Float64Bits>) return literal;
    else throw ValueImportFailure{PhaseError::kOperandMismatch, {}};
  }, value);
}
}  // namespace abcd::analysis::detail
