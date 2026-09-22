#ifndef ABCD_SOURCE_LOCAL_LEXICAL_BODY_H_
#define ABCD_SOURCE_LOCAL_LEXICAL_BODY_H_

#include "abcd/abc/method_lookup.h"
#include "abcd/analysis/linear_lexicals.h"
#include "abcd/source/linear_body.h"

namespace abcd::source {
struct LocalLexicalBudget {
  std::size_t bytes = 64 * 1024 * 1024;
  std::uint64_t work =
      100000000;  // Plan quota; graph/environment passes are separate.
  EmitBudget output;
};
using LocalLexicalResult =
    std::variant<std::size_t, BodyEmitFailure, analysis::FlowFailure,
                 analysis::EnvironmentFlowFailure,
                 analysis::LinearLexicalFailure>;
// Straight-line local lexical recovery; same register ABI and constant-file
// contracts as WriteLinearBody. Does not infer a function wrapper or record.
// The prefix $lex is reserved for generated function-local cell names.
// Register/acc bindings are private temporaries, not exposed to called code.
// Analysis graph/environment/plan share bytes. Output streams in two passes;
// semantic/budget/binding failures leave output untouched, not IO/file changes.
// Inputs, name storage and transient constants have separately admitted costs.
// Optional methods plus constants enable independent ordinary leaf functions.
// Targets are verified and source artifacts cached by target/length, not
// runtime objects. Cache and preparation scratch share bytes. No recursive
// creations.
[[nodiscard]] LocalLexicalResult WriteLocalLexicalBody(
    const abc::MethodBody& body, ScalarNames names, std::ostream& output,
    LocalLexicalBudget budget = {}, const MethodConstants* constants = nullptr,
    const abc::MethodLookup* methods = nullptr);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_LOCAL_LEXICAL_BODY_H_
