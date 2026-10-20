#ifndef ABCD_SOURCE_SOURCE_VARIABLES_H_
#define ABCD_SOURCE_SOURCE_VARIABLES_H_
#include "binding_initializer.h"
#include "brace_plan.h"
#include "readable_statements.h"
#include "loop_syntax.h"
#include "nullish_syntax.h"
namespace abcd::source {
struct OriginalFlow;
// One preparation result for one unchanged checked tree. Borrowed tree identity
// scopes reuse to this preparation lifetime, never a cross-revision cache.
// Consumers receive const access; changes require a fresh preparation result.
struct SourceVariables {
  const FunctionTree* tree = nullptr;
  const OriginalFlow* original = nullptr;
  NullishSyntax nullish;
  LoopSyntax loops;
  BindingInitializers bindings;
  BracePlan braces;
  ReadableStatements readable;
  std::vector<std::optional<ExprId>> readable_equalities;
  // Peak preparation admission and conservative returned storage are distinct.
  // Callers admit the peak before releasing destroyed preparation scratch.
  std::size_t allocated_bytes = 0, retained_bytes = 0;
  std::uint64_t work = 0;
};
// Dependency order: syntax projections precede binding recovery; class-self
// naming and emission then consume the same final binding projection. This
// coordinator supplies the retained original input to the same revision-owned
// facts. Absence preserves the conservative language-only adapter.
[[nodiscard]] std::variant<SourceVariables,TreeStructureError> PrepareSourceVariables(
    const FunctionTree&,analysis::SemanticBudget = {},const OriginalFlow* = nullptr);
}
#endif
