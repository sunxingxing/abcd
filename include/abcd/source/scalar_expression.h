#ifndef ABCD_SOURCE_SCALAR_EXPRESSION_H_
#define ABCD_SOURCE_SCALAR_EXPRESSION_H_

#include <iosfwd>
#include <span>
#include <string_view>

#include "abcd/analysis/scalar.h"
#include "abcd/source/emission.h"

namespace abcd::source {
struct ScalarNames {
  std::string_view accumulator;
  std::span<const std::string_view> registers;
};
// Emits an expression, not a method/record. Referenced names must be declared,
// initialized, side-effect-free local bindings supplied by the caller. Only
// strict-safe ASCII identifiers are accepted; nothing is silently renamed.
// Raw binary64 NaNs are rejected rather than discarding payload bits.
// O(referenced name bytes), fixed scratch; budget/validation failure writes
// nothing. A stream failure may leave partial output. Inputs must remain
// stable.
[[nodiscard]] std::variant<std::size_t, EmitError> WriteScalarExpression(
    const analysis::ScalarExpression& expression, ScalarNames names,
    std::ostream& output, EmitBudget budget = {});
}  // namespace abcd::source
#endif
