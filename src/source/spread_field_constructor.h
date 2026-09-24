#ifndef ABCD_SOURCE_SPREAD_FIELD_CONSTRUCTOR_H_
#define ABCD_SOURCE_SPREAD_FIELD_CONSTRUCTOR_H_

#include "abcd/analysis/derived_constructor.h"
#include "abcd/source/base_function.h"

namespace abcd::source {
// Internal class-body emitter. Caller proves the constructor target, ABI and
// JSFunction-branded parent; native spread cannot preserve v7's Proxy refusal.
// the two-pass writer validates all constants before publishing source.
[[nodiscard]] FunctionEmitResult WriteSpreadFieldConstructor(
    const analysis::SpreadFieldDerivedConstructor& proof,
    std::uint16_t length, const MethodConstants& constants,
    std::ostream& output, EmitBudget budget = {});
}  // namespace abcd::source

#endif  // ABCD_SOURCE_SPREAD_FIELD_CONSTRUCTOR_H_
