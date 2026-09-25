#ifndef ABCD_SOURCE_ACCUMULATOR_LIVENESS_H_
#define ABCD_SOURCE_ACCUMULATOR_LIVENESS_H_

#include "semantic_ast_input.h"

namespace abcd::source {
enum class AccumulatorAccess : std::uint8_t { kNone, kRead, kWrite };
struct AccumulatorLiveness {
  std::vector<AccumulatorAccess> accesses;
  std::vector<std::uint8_t> after;
};
// Final emitted operations, not original SSA. Unknown owners count as reads.
std::variant<AccumulatorLiveness, SourceFailure> FindAccumulatorLiveness(
    const AstMethod& method, const SourceAst& ast, std::size_t budget_bytes);
}  // namespace abcd::source
#endif
