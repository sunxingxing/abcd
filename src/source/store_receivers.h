#ifndef ABCD_SOURCE_STORE_RECEIVERS_H_
#define ABCD_SOURCE_STORE_RECEIVERS_H_

#include "semantic_ast_input.h"

namespace abcd::source {
// Run after stored-value folding, before identity-based reload elision.
// Uses must include exceptional phi inputs. No repeated backward searches.
void FoldStoreReceivers(const AstMethod& method, SourceAst& ast,
                        std::span<const std::size_t> uses);
}  // namespace abcd::source
#endif
