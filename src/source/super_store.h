#ifndef ABCD_SOURCE_SUPER_STORE_H_
#define ABCD_SOURCE_SUPER_STORE_H_
#include "abcd/source/source_tree.h"
#include "tree_checks.h"
namespace abcd::source {
// Ark resolves GetSuperBase/ToPropertyKey at PutValue, after the RHS. Native
// JS resolves them before the RHS. This index proves that intervening RHS
// evaluation has no effect or language exception; key domains are separate.
// Built once per verified tree, O(expressions + child edges), prepaid storage.
[[nodiscard]] std::vector<std::uint8_t> SuperStoreRhsSafety(
    const FunctionTree&, TreeCheckBudget&);
}
#endif
