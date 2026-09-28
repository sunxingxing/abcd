#include "evaluation_plan.h"

#include <array>
#include <type_traits>

namespace abcd::source {
namespace {
using Role = analysis::OperandRole;
constexpr std::array<Role, 0> kNoOperands{};
constexpr std::array kInput{Role::kInput};
constexpr std::array kBinary{Role::kLeft, Role::kRight};
constexpr std::array kNamedRead{Role::kBase};
constexpr std::array kPropertyRead{Role::kBase, Role::kKey};
constexpr std::array kNamedWrite{Role::kBase, Role::kStoredValue};
constexpr std::array kPropertyWrite{Role::kBase, Role::kKey, Role::kStoredValue};
constexpr std::array kBranch{Role::kCondition};
constexpr std::array kReturn{Role::kReturnValue};
constexpr std::array kThrow{Role::kThrownValue};
constexpr std::array kCall{Role::kCallee};
constexpr std::array kReceiverCall{Role::kCallee, Role::kReceiver};
constexpr std::array kConstruct{Role::kConstructor};
constexpr std::array kCellStore{Role::kStoredValue};
}  // namespace

std::optional<EvaluationShape> DescribeEvaluation(const analysis::ValueOperation& operation) {
  return std::visit([](const auto& op) -> std::optional<EvaluationShape> {
    using T = std::decay_t<decltype(op)>;
    if constexpr (std::is_same_v<T, analysis::ValueCopy> || std::is_same_v<T, analysis::ValueUnary>)
      return EvaluationShape{kInput, true};
    else if constexpr (std::is_same_v<T, analysis::ValueBinary>) return EvaluationShape{kBinary, true};
    else if constexpr (std::is_same_v<T, analysis::ValueNamedRead>) return EvaluationShape{kNamedRead, true};
    else if constexpr (std::is_same_v<T, analysis::ValueLexicalLoad>) return EvaluationShape{kNoOperands, true};
    else if constexpr (std::is_same_v<T, analysis::ValueModuleRead>) return EvaluationShape{kNoOperands, true};
    else if constexpr (std::is_same_v<T, analysis::ValueModuleWrite>) return EvaluationShape{kCellStore, false};
    else if constexpr (std::is_same_v<T, analysis::ValueLexicalStore>) return EvaluationShape{kCellStore, false};
    else if constexpr (std::is_same_v<T, analysis::ValuePropertyRead>) return EvaluationShape{kPropertyRead, true};
    else if constexpr (std::is_same_v<T, analysis::ValuePropertyWrite>) {
      EvaluationShape shape{kPropertyWrite, false};
      shape.reference = ReferenceBoundary::kBeforeStoredValue;
      return shape;
    }
    else if constexpr (std::is_same_v<T, analysis::ValueNamedWrite>) return EvaluationShape{kNamedWrite, false};
    else if constexpr (std::is_same_v<T, analysis::ValueOwnWrite>) {
      switch (op.kind) {
        case analysis::OwnWriteKind::kStore: case analysis::OwnWriteKind::kDefineField:
          return EvaluationShape{kNamedWrite, false};
      }
      return {};
    }
    else if constexpr (std::is_same_v<T, analysis::ValueBranch>) return EvaluationShape{kBranch, false};
    else if constexpr (std::is_same_v<T, analysis::ValueReturn>) return EvaluationShape{kReturn, false};
    else if constexpr (std::is_same_v<T, analysis::ValueThrow>) return EvaluationShape{kThrow, false};
    else if constexpr (std::is_same_v<T, analysis::ValueJump>) return EvaluationShape{kNoOperands, false};
    else if constexpr (std::is_same_v<T, analysis::ValueDebugger>) return EvaluationShape{kNoOperands, false};
    else if constexpr (std::is_same_v<T, analysis::ValueFrameThis>) return EvaluationShape{kNoOperands, true};
    else if constexpr (std::is_same_v<T, analysis::ValueFrameFunction>) return EvaluationShape{kNoOperands, true};
    else if constexpr (std::is_same_v<T, analysis::ValueGlobalObject>) return EvaluationShape{kNoOperands, true};
    else if constexpr (std::is_same_v<T, analysis::ValueSymbolConstructor>) return EvaluationShape{kNoOperands, true};
    else if constexpr (std::is_same_v<T, analysis::ValueArgumentSnapshot>) {
      EvaluationShape shape{kNoOperands, true, op.slots};
      shape.inputs = InputEvaluation::kConditionalArgument;
      return shape;
    }
    else if constexpr (std::is_same_v<T, analysis::ValueString> || std::is_same_v<T, analysis::ValueHole>)
      return EvaluationShape{kNoOperands, true};
    else if constexpr (std::is_same_v<T, analysis::ValueFunctionCreation>) {
      if (op.kind == analysis::CreationKind::kFunction && op.home_object == analysis::HomeObjectInput::kCurrentFunction)
        return EvaluationShape{kNoOperands, true};
      return {};
    }
    else if constexpr (std::is_same_v<T, analysis::ValueObjectAllocation>) {
      return EvaluationShape{kNoOperands, true};
    }
    else if constexpr (std::is_same_v<T, analysis::ValueArrayAllocation>)
      return EvaluationShape{kNoOperands, true};
    else if constexpr (std::is_same_v<T, analysis::ValueGlobalRead>) {
      switch (op.lookup) {
        case analysis::GlobalLookup::kName: case analysis::GlobalLookup::kVar:
          return EvaluationShape{kNoOperands, true};
      }
      return {};
    }
    else if constexpr (std::is_same_v<T, analysis::ValueInvoke>) {
      switch (op.receiver) {
        case analysis::InvokeReceiver::kUndefined: return EvaluationShape{kCall, true, op.arguments};
        case analysis::InvokeReceiver::kExplicit: return EvaluationShape{kReceiverCall, true, op.arguments};
      }
      return {};
    } else if constexpr (std::is_same_v<T, analysis::ValueConstruct>)
      return EvaluationShape{kConstruct, true, op.arguments};
    else {
      static_assert(std::is_same_v<T, analysis::ValueGuard> || std::is_same_v<T, analysis::ValueHole> ||
                    std::is_same_v<T, analysis::ValueLexicalLoad> || std::is_same_v<T, analysis::ValueLexicalStore> ||
                    std::is_same_v<T, analysis::ValueEnvironmentPush> || std::is_same_v<T, analysis::ValueEnvironmentPop> ||
                    std::is_same_v<T, analysis::ValueCurrentEnvironment> ||
                    std::is_same_v<T, analysis::ValueNop>);
      return {};
    }
  }, operation);
}

std::optional<EvaluationError> CheckArgumentSnapshot(
    const analysis::ValueProgram& graph, const analysis::ValueOp& operation) {
  const auto* snapshot = std::get_if<analysis::ValueArgumentSnapshot>(&operation.operation);
  if (!snapshot) return {};
  if (!graph.arguments || !std::holds_alternative<analysis::ArgumentWindow>(
      analysis::ArgumentWindow::Build(graph.arguments->frame)) ||
      snapshot->slots != graph.arguments->frame.parameters)
    return EvaluationError::kOwnership;
  constexpr auto kEffects = analysis::LanguageEffect::kArgumentsRead |
      analysis::LanguageEffect::kAllocation;
  if (operation.normal != kEffects || operation.before_throw != kEffects ||
      operation.exceptional != analysis::ExceptionalCompletion::kMayThrow)
    return EvaluationError::kInvalidGraph;
  return {};
}

std::optional<EvaluationError> CheckEvaluationUse(
    const analysis::ValueProgram& graph, const analysis::OperandUse& use,
    const EvaluationShape& shape, std::uint32_t ordinal) {
  const bool conditional = shape.inputs == InputEvaluation::kConditionalArgument;
  const auto purpose = conditional ? analysis::ReadPurpose::kProtocol : analysis::ReadPurpose::kOperand;
  if (use.purpose != purpose) return EvaluationError::kProtocolOperand;
  if (use.role != shape.Role(ordinal)) return EvaluationError::kInvalidGraph;
  if (conditional) {
    const auto* value = std::get_if<analysis::ValueId>(&use.operand);
    if (!value || value->value >= graph.values.size() || !use.input ||
        use.input->ordinal != ordinal ||
        !graph.values[value->value].domain.IsLanguage())
      return EvaluationError::kInvalidGraph;
  }
  return {};
}

bool OrderedEvaluation(const analysis::ValueOp& op) {
  if (op.normal != analysis::LanguageEffect::kNone ||
      op.before_throw != analysis::LanguageEffect::kNone ||
      op.exceptional != analysis::ExceptionalCompletion::kImpossible) return true;
  // Keep memory/reference reads ordered even if a malformed effects contract
  // reaches this boundary. Imported semantic effects remain independently checked.
  return std::visit([](const auto& operation) {
    using T = std::decay_t<decltype(operation)>;
    if constexpr (std::is_same_v<T, analysis::ValueCopy> ||
                  std::is_same_v<T, analysis::ValueUnary> ||
                  std::is_same_v<T, analysis::ValueBinary> ||
                  std::is_same_v<T, analysis::ValueString> || std::is_same_v<T, analysis::ValueHole>) return false;
    else {
      static_assert(std::is_same_v<T, analysis::ValueNamedRead> ||
                    std::is_same_v<T, analysis::ValueDebugger> ||
                    std::is_same_v<T, analysis::ValueFrameThis> ||
                    std::is_same_v<T, analysis::ValueFrameFunction> ||
                    std::is_same_v<T, analysis::ValueGlobalObject> ||
                    std::is_same_v<T, analysis::ValueSymbolConstructor> ||
                    std::is_same_v<T, analysis::ValueArgumentSnapshot> ||
                    std::is_same_v<T, analysis::ValueModuleRead> ||
                    std::is_same_v<T, analysis::ValueModuleWrite> ||
                    std::is_same_v<T, analysis::ValueFunctionCreation> ||
                    std::is_same_v<T, analysis::ValueLexicalLoad> || std::is_same_v<T, analysis::ValueLexicalStore> ||
                    std::is_same_v<T, analysis::ValueEnvironmentPush> || std::is_same_v<T, analysis::ValueEnvironmentPop> ||
                    std::is_same_v<T, analysis::ValueCurrentEnvironment> ||
                    std::is_same_v<T, analysis::ValueGlobalRead> ||
                    std::is_same_v<T, analysis::ValuePropertyRead> ||
                    std::is_same_v<T, analysis::ValuePropertyWrite> ||
                    std::is_same_v<T, analysis::ValueNamedWrite> ||
                    std::is_same_v<T, analysis::ValueOwnWrite> ||
                    std::is_same_v<T, analysis::ValueBranch> ||
                    std::is_same_v<T, analysis::ValueReturn> ||
                    std::is_same_v<T, analysis::ValueThrow> || std::is_same_v<T, analysis::ValueHole> ||
                    std::is_same_v<T, analysis::ValueJump> ||
                    std::is_same_v<T, analysis::ValueGuard> ||
                    std::is_same_v<T, analysis::ValueInvoke> || std::is_same_v<T, analysis::ValueConstruct> ||
                    std::is_same_v<T, analysis::ValueArrayAllocation> ||
                    std::is_same_v<T, analysis::ValueObjectAllocation> ||
                    std::is_same_v<T, analysis::ValueNop>);
      return true;
    }
  }, op.operation);
}
}  // namespace abcd::source
