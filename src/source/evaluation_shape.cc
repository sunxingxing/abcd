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
}  // namespace

std::optional<EvaluationShape> DescribeEvaluation(const analysis::ValueOperation& operation) {
  return std::visit([](const auto& op) -> std::optional<EvaluationShape> {
    using T = std::decay_t<decltype(op)>;
    if constexpr (std::is_same_v<T, analysis::ValueCopy> || std::is_same_v<T, analysis::ValueUnary>)
      return EvaluationShape{kInput, true};
    else if constexpr (std::is_same_v<T, analysis::ValueBinary>) return EvaluationShape{kBinary, true};
    else if constexpr (std::is_same_v<T, analysis::ValueNamedRead>) return EvaluationShape{kNamedRead, true};
    else if constexpr (std::is_same_v<T, analysis::ValuePropertyRead>) return EvaluationShape{kPropertyRead, true};
    else if constexpr (std::is_same_v<T, analysis::ValueNamedWrite>) return EvaluationShape{kNamedWrite, false};
    else if constexpr (std::is_same_v<T, analysis::ValueBranch>) return EvaluationShape{kBranch, false};
    else if constexpr (std::is_same_v<T, analysis::ValueReturn>) return EvaluationShape{kReturn, false};
    else if constexpr (std::is_same_v<T, analysis::ValueJump>) return EvaluationShape{kNoOperands, false};
    else if constexpr (std::is_same_v<T, analysis::ValueString>) return EvaluationShape{kNoOperands, true};
    else {
      static_assert(std::is_same_v<T, analysis::ValueGuard> ||
                    std::is_same_v<T, analysis::ValueArrayAllocation> ||
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
                    std::is_same_v<T, analysis::ValuePropertyRead> ||
                    std::is_same_v<T, analysis::ValueNamedWrite> ||
                    std::is_same_v<T, analysis::ValueBranch> ||
                    std::is_same_v<T, analysis::ValueReturn> ||
                    std::is_same_v<T, analysis::ValueJump> ||
                    std::is_same_v<T, analysis::ValueGuard> ||
                    std::is_same_v<T, analysis::ValueArrayAllocation> ||
                    std::is_same_v<T, analysis::ValueNop>);
      return true;
    }
  }, op.operation);
}
}  // namespace abcd::source
