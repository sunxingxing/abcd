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
constexpr std::array kBranch{Role::kCondition};
constexpr std::array kReturn{Role::kReturnValue};
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
    else if constexpr (std::is_same_v<T, analysis::ValueLexicalStore>) return EvaluationShape{kCellStore, false};
    else if constexpr (std::is_same_v<T, analysis::ValuePropertyRead>) return EvaluationShape{kPropertyRead, true};
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
    else if constexpr (std::is_same_v<T, analysis::ValueJump>) return EvaluationShape{kNoOperands, false};
    else if constexpr (std::is_same_v<T, analysis::ValueString>) return EvaluationShape{kNoOperands, true};
    else if constexpr (std::is_same_v<T, analysis::ValueFunctionCreation>) {
      if (op.kind == analysis::CreationKind::kFunction && op.home_object == analysis::HomeObjectInput::kCurrentFunction)
        return EvaluationShape{kNoOperands, true};
      return {};
    }
    else if constexpr (std::is_same_v<T, analysis::ValueArrayAllocation> ||
                       std::is_same_v<T, analysis::ValueObjectAllocation>)
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
      static_assert(std::is_same_v<T, analysis::ValueGuard> ||
                    std::is_same_v<T, analysis::ValueLexicalLoad> || std::is_same_v<T, analysis::ValueLexicalStore> ||
                    std::is_same_v<T, analysis::ValueEnvironmentPush> || std::is_same_v<T, analysis::ValueEnvironmentPop> ||
                    std::is_same_v<T, analysis::ValueCurrentEnvironment> ||
                    std::is_same_v<T, analysis::ValueNop>);
      return {};
    }
  }, operation);
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
                  std::is_same_v<T, analysis::ValueString>) return false;
    else {
      static_assert(std::is_same_v<T, analysis::ValueNamedRead> ||
                    std::is_same_v<T, analysis::ValueFunctionCreation> ||
                    std::is_same_v<T, analysis::ValueLexicalLoad> || std::is_same_v<T, analysis::ValueLexicalStore> ||
                    std::is_same_v<T, analysis::ValueEnvironmentPush> || std::is_same_v<T, analysis::ValueEnvironmentPop> ||
                    std::is_same_v<T, analysis::ValueCurrentEnvironment> ||
                    std::is_same_v<T, analysis::ValueGlobalRead> ||
                    std::is_same_v<T, analysis::ValuePropertyRead> ||
                    std::is_same_v<T, analysis::ValueNamedWrite> ||
                    std::is_same_v<T, analysis::ValueOwnWrite> ||
                    std::is_same_v<T, analysis::ValueBranch> ||
                    std::is_same_v<T, analysis::ValueReturn> ||
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
