#ifndef ABCD_SOURCE_LINEAR_BODY_H_
#define ABCD_SOURCE_LINEAR_BODY_H_

#include "abcd/abc/method_body.h"
#include "abcd/abc/string_entity.h"
#include "abcd/source/scalar_expression.h"

namespace abcd::source {
enum class BodyEmitError {
  kExceptionRegions,
  kUnsupportedInstruction,
  kAccumulatorHole,
  kMissingTerminator,
  kTrailingInstructions,
  kExpression,
  kMissingConstants,
  kConstant,
  kIndexedName
};
struct BodyEmitFailure {
  BodyEmitError kind;
  abc::CodeOffset offset;
  std::optional<analysis::ScalarError> scalar_error;
  std::optional<EmitError> emit_error;
  std::optional<abc::MetadataError> metadata_error = {};
};
using BodyEmitResult = std::variant<std::size_t, BodyEmitFailure>;

struct MethodConstants {
  const corpus::AbcFile& file;
  const abc::Header& header;
  const abc::MetadataIndex& index;
  abc::EntityOffset method;
  abc::StringBudget string_budget;
};

// Emits statements inside a function, not a wrapper or record. Requires a
// validated decoded method. Caller supplies distinct writable local bindings
// for all registers and accumulator, initializes registers according to ABI,
// and guarantees language values (no VM sentinels). Accumulator reads must be
// dominated by a write here; caller initialization cannot bypass that check.
// Only straight-line scalar/string/property reads, strict named stores,
// fixed/narrow-range ordinary calls, nop, return and unconditional throw are
// supported. Exceptions, branches and trailing code are
// explicit errors, as are canonical element names on named property operations
// whose pinned fast path is not equivalent to native JS indexed access.
// Full preflight leaves output untouched on semantic/budget/binding failure;
// IO failure may leave a prefix. String loads
// require same-file constants and one transient string allowance within the
// process memory budget. Work is O(instructions * log(regions) + visited string
// bytes + emitted bytes), with no body-sized output buffer. Inputs must stay
// stable across both passes; second-pass read failures can leave a prefix and
// require atomic output.
[[nodiscard]] BodyEmitResult WriteLinearBody(
    const abc::MethodBody& body, ScalarNames names, std::ostream& output,
    EmitBudget budget = {}, const MethodConstants* constants = nullptr);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_LINEAR_BODY_H_
