#ifndef ABCD_SOURCE_TREE_GLOBALS_H_
#define ABCD_SOURCE_TREE_GLOBALS_H_

#include "abcd/source/global_reference.h"
#include "abcd/source/source_tree.h"

namespace abcd::source {
enum class TreeGlobalError : std::uint8_t { kMissingContext, kName, kShadowed, kResourceLimit, kWorkLimit };
struct TreeGlobals { std::size_t allocated_bytes; std::uint64_t work; };
// GlobalScope is the existing caller-proven complete generated enclosure, not
// inferred from ABC global names. No context admits only trees without globals.
// Its record-owned sorted indexes are borrowed, never rescanned per method.
// Recheck at every publication into a new enclosure; do not cache this report
// as a transferable certificate. Scratch borrows names only during this call.
[[nodiscard]] std::variant<TreeGlobals, TreeGlobalError> VerifyTreeGlobals(
    const FunctionTree& tree, const GlobalScope* scope,
    analysis::SemanticBudget budget = {});
}  // namespace abcd::source
#endif
