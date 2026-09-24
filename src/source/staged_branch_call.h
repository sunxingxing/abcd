#ifndef ABCD_SOURCE_STAGED_BRANCH_CALL_H_
#define ABCD_SOURCE_STAGED_BRANCH_CALL_H_

#include "abcd/analysis/named_invocation.h"
#include "abcd/source/local_lexical_body.h"
#include "branch_source_facts.h"

namespace abcd::source {
// The caller has proved all original CFG register reads language-valued.
// This additionally proves the getter/callee/call interval is a single-entry
// argument region and emits a native receiver call around its ordered body.
[[nodiscard]] AcyclicResult WriteStagedBranchCall(
    const abc::MethodBody& body, const analysis::StagedNamedInvocation& call,
    ScalarNames names, std::ostream& output, LocalLexicalBudget budget,
    const MethodConstants* constants, const BranchSourceFacts& outer);
}  // namespace abcd::source

#endif  // ABCD_SOURCE_STAGED_BRANCH_CALL_H_
