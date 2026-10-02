#ifndef ABCD_SOURCE_INTEGER_NAN_H_
#define ABCD_SOURCE_INTEGER_NAN_H_
#include "evaluation_plan.h"
#include <bit>
#include <limits>
namespace abcd::source {
struct IntegerNaNCost { std::size_t bytes; std::uint64_t work; };
[[nodiscard]] bool IntegerCoercion(const analysis::ValueOperation&);
[[nodiscard]] bool ErasableNaN(const analysis::ValueOperand&);
constexpr std::uint64_t CanonicalNaNBits() {
  return std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN());
}
[[nodiscard]] std::variant<IntegerNaNCost, EvaluationError> BuildIntegerNaNs(
    const analysis::ValueProgram&, EvaluationPlan&, analysis::SemanticBudget);
[[nodiscard]] std::variant<std::uint64_t, EvaluationError> VerifyIntegerNaNs(
    const analysis::ValueProgram&, const EvaluationPlan&, analysis::SemanticBudget);
}
#endif
