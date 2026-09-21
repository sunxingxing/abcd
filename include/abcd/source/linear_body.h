#ifndef ABCD_SOURCE_LINEAR_BODY_H_
#define ABCD_SOURCE_LINEAR_BODY_H_

#include "abcd/abc/method_body.h"
#include "abcd/source/scalar_expression.h"

namespace abcd::source {
enum class BodyEmitError {
  kExceptionRegions,
  kUnsupportedInstruction,
  kAccumulatorHole,
  kMissingTerminator,
  kTrailingInstructions,
  kExpression
};
struct BodyEmitFailure {
  BodyEmitError kind;
  abc::CodeOffset offset;
  std::optional<analysis::ScalarError> scalar_error;
  std::optional<EmitError> emit_error;
};
using BodyEmitResult = std::variant<std::size_t, BodyEmitFailure>;

// Emits statements inside a function, not a wrapper or record. Requires a
// validated decoded method. Caller supplies distinct writable local bindings
// for all registers and accumulator, initializes registers according to ABI,
// and guarantees language values (no VM sentinels). Accumulator reads must be
// dominated by a write here; caller initialization cannot bypass that check.
// Only straight-line scalar assignments, nop, return and unconditional throw
// are supported. Exceptions, branches and trailing code are explicit errors.
// Full preflight leaves output untouched on semantic/budget/binding failure;
// IO failure may leave a prefix. O(instructions + emitted bytes), fixed
// scratch, no body-sized output buffer. Caller must keep inputs stable across
// both passes.
[[nodiscard]] BodyEmitResult WriteLinearBody(const abc::MethodBody& body,
                                             ScalarNames names,
                                             std::ostream& output,
                                             EmitBudget budget = {});
}  // namespace abcd::source
#endif  // ABCD_SOURCE_LINEAR_BODY_H_
