#ifndef ABCD_ANALYSIS_REGISTER_VALUE_RULE_H_
#define ABCD_ANALYSIS_REGISTER_VALUE_RULE_H_

#include "abcd/analysis/register_effect.h"
#include "abcd/analysis/value_kinds.h"

namespace abcd::analysis {
struct SemanticInstruction;
}
namespace abcd::analysis::detail {
enum class ValueRuleKind {
  kFixed,
  kJoin,
  kLanguageOperation,
  kStringAdd,
  kNonNaNUnary
};
struct ValueRule {
  ValueRuleKind kind = ValueRuleKind::kFixed;
  ValueKinds seed;
};
// Classifies a definite normal-path register definition. Join is one copy;
// language operations have at most two scalar operands. Property/call/lexical
// results stay unknown absent heap/callee/cell evidence, not optimistic JS
// types.
std::variant<ValueRule, ScalarError> DescribeRegisterValue(
    const abc::DecodedInstruction& instruction, std::uint64_t frame_size);
ValueRule DescribeScalarValue(const ScalarAssignment& assignment);
std::variant<ValueRule, ScalarError> DescribeRegisterValue(
    const SemanticInstruction& instruction);
}  // namespace abcd::analysis::detail
#endif  // ABCD_ANALYSIS_REGISTER_VALUE_RULE_H_
