#ifndef ABCD_SOURCE_LEXICAL_STORES_H_
#define ABCD_SOURCE_LEXICAL_STORES_H_

#include "capture_tree.h"
#include "phased_body.h"

namespace abcd::source {
struct NormalStoreInput { const FunctionInput& function; };
struct PhasedStoreInput { const PhasedBody& function; const abc::MethodBody& body; };
using LexicalStoreInput = std::variant<NormalStoreInput, PhasedStoreInput>;
enum class LexicalStoreError : std::uint8_t {
  kOwner, kCoverage, kDomain, kStore, kResourceLimit, kWorkLimit
};
struct LexicalStoreFailure {
  LexicalStoreError kind;
  std::optional<abc::CodeOffset> pc;
};
struct LexicalStores {
  std::size_t stores = 0, captured = 0, signature = 0;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Borrow unchanged original input, working cells and independently checked
// tree/flow. Recheck ALL original store operands and actual assignment targets.
// Source and tree CellIds belong to distinct plans. Join through original sites
// and resolved storage/allocation identity, including demanded/forwarded cells.
// Normal signature flags require a fresh default-signature proof; phased
// signature migration is not admitted. This is a member obligation only, not
// complete child coverage, initialized captures or a source publication
// receipt. O(N+I+O+A+C+U+T+S log(R+1)) work, O(I+A+U+T) scratch: phase
// nodes, instructions, working ops, allocations, final cells, checked inputs,
// tree statements, stores and original SSA reads. Environment ancestry has
// fixed 32-bit depth. No per-store walks.
[[nodiscard]] std::variant<LexicalStores, LexicalStoreFailure> VerifyLexicalStores(
    LexicalStoreInput input, const analysis::ValueProgram& graph, const CellPlan* cells,
    CaptureTree target, std::span<const std::uint8_t> signature = {},
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
