#ifndef ABCD_SOURCE_FUNCTION_ABI_BUILD_H_
#define ABCD_SOURCE_FUNCTION_ABI_BUILD_H_

#include "abcd/source/function_abi.h"

namespace abcd::source {
struct EvaluationPlan;
enum class FunctionAbiError : std::uint8_t {
  kRevision, kParameter, kEntry, kReceiver, kSyntax, kResourceLimit, kWorkLimit,
  kLength, kEvaluation
};
struct ConstructedAbi {
  FunctionAbi abi;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Requires verified value/tree correspondence and a same-input frame. Binds
// that result to wrapper-facing usage/signature facts in one O(S+U+X+P+I) scan;
// no per-parameter graph walk or source-text analysis. S is symbol count,
// U is operand-use count, X is expression count, P is formal count and I is
// operation count. Frame-receiver reads are counted separately from ABI uses.
// Literal defaults are shape-checked and determine native .length. This
// census does not prove default-prologue movement; the owner must separately
// establish body correspondence and the parameter-environment contract.
// Explicit capture symbols are free bindings, not ABI parameters. This census
// does not discharge their enclosing-owner or creation-site obligations.
// An arrow cannot observe a dynamic receiver entry or an owner-local ThisExpr;
// lexical receiver recovery requires a separate explicit capture binding.
[[nodiscard]] std::variant<ConstructedAbi, FunctionAbiError> BuildFunctionAbi(
    const analysis::ValueProgram& graph, const FunctionTree& tree,
    const abc::FrameLayout& frame, std::uint32_t length,
    analysis::SemanticBudget budget = {});
// Same census with a freshly checked phased source projection. Pure elisions
// suppress only their own operand reads; phi/catch and retained uses still count.
[[nodiscard]] std::variant<ConstructedAbi, FunctionAbiError> BuildPhasedFunctionAbi(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation, const FunctionTree& tree,
    const abc::FrameLayout& frame, std::uint32_t length,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
