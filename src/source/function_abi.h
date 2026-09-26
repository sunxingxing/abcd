#ifndef ABCD_SOURCE_FUNCTION_ABI_BUILD_H_
#define ABCD_SOURCE_FUNCTION_ABI_BUILD_H_

#include "abcd/source/function_abi.h"

namespace abcd::source {
enum class FunctionAbiError : std::uint8_t {
  kRevision, kParameter, kEntry, kReceiver, kSyntax, kResourceLimit, kWorkLimit
};
struct ConstructedAbi {
  FunctionAbi abi;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Requires verified value/tree correspondence and a same-input frame. Binds
// that result to wrapper-facing usage/signature facts in one O(S+U+X+P) scan;
// no per-parameter graph walk or source-text analysis. S is symbol count,
// U is operand-use count, X is expression count and P is formal count.
// Literal defaults are shape-checked and determine native .length. This
// census does not prove default-prologue movement; the owner must separately
// establish body correspondence and the parameter-environment contract.
// Explicit capture symbols are free bindings, not ABI parameters. This census
// does not discharge their enclosing-owner or creation-site obligations.
[[nodiscard]] std::variant<ConstructedAbi, FunctionAbiError> BuildFunctionAbi(
    const analysis::ValueProgram& graph, const FunctionTree& tree,
    const abc::FrameLayout& frame, std::uint32_t length,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
