#ifndef ABCD_SOURCE_BRANCH_VALUE_PROOF_H_
#define ABCD_SOURCE_BRANCH_VALUE_PROOF_H_

#include "abcd/analysis/register_value_flow.h"
#include "abcd/source/local_lexical_body.h"

namespace abcd::source {
// The ordered emitter relies on caller-proven language values. Function
// wrappers must discharge that proof for every reachable CFG read before
// publishing source; this is not a general exceptional-control-flow proof.
[[nodiscard]] std::optional<BodyEmitFailure> ProveBranchValues(
    const abc::MethodBody& body, std::uint64_t frame_size,
    LocalLexicalBudget budget,
    std::span<const analysis::LanguageLoadFact> loads = {});
}  // namespace abcd::source
#endif  // ABCD_SOURCE_BRANCH_VALUE_PROOF_H_
