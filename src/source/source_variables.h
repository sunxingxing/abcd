#ifndef ABCD_SOURCE_SOURCE_VARIABLES_H_
#define ABCD_SOURCE_SOURCE_VARIABLES_H_
#include "binding_initializer.h"
#include "loop_syntax.h"
#include "nullish_syntax.h"
namespace abcd::source {
// One preparation result for one unchanged checked tree. Borrowed tree identity
// scopes reuse to this preparation lifetime, never a cross-revision cache.
// Consumers receive const access; changes require a fresh preparation result.
struct SourceVariables {
  const FunctionTree* tree = nullptr;
  NullishSyntax nullish;
  LoopSyntax loops;
  BindingInitializers bindings;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Dependency order: syntax projections precede binding recovery; class-self
// naming and emission then consume the same final binding projection. This
// coordinator does not yet replace the legacy initialization census inside it.
[[nodiscard]] std::variant<SourceVariables,TreeStructureError> PrepareSourceVariables(
    const FunctionTree&,analysis::SemanticBudget = {});
}
#endif
