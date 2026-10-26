#ifndef ABCD_SOURCE_ARRAY_PATTERN_SURFACE_H_
#define ABCD_SOURCE_ARRAY_PATTERN_SURFACE_H_
#include "binding_initializer.h"
#include "loop_syntax.h"
#include "readable_statements.h"
namespace abcd::source {
class TreeCheckBudget;
struct ArrayPatternTarget {
  StmtId statement;
  // Absent: elision (the step result is never read).
  std::optional<SymbolId> symbol;
};
// One certified contiguous run `r = Acquire(e); [t_k =] Step(r)...; Close(r)`
// of retained ExpressionStmts in one block (array-patterns.md). It prints as
// native `[t_0, , t_n] = e` or, when every bound target is a declaration
// initializer, as one `let`/`const` pattern declaration at the acquire.
struct ArrayPatternSurface {
  StmtId acquire, close;
  ExprId iterable;
  std::uint32_t first_target = 0, target_count = 0;
  std::optional<DeclarationKind> declaration;
};
struct ArrayPatternSurfaces {
  std::vector<ArrayPatternSurface> patterns;
  std::vector<ArrayPatternTarget> targets;
  // Statement indexed: acquire statement -> pattern; folded step/close members.
  std::vector<std::optional<std::uint32_t>> statements;
  std::vector<std::uint8_t> members;
};
// Printer-only projection over the final binding/readable plans. Runs that do
// not match exactly are left out; their ArrayPatternExpr nodes then refuse to
// print. O(S + E) for S statements and E expressions.
[[nodiscard]] ArrayPatternSurfaces PlanArrayPatternSurfaces(const FunctionTree&,
    const BindingInitializers&, const LoopSyntax&, const ReadableStatements&, TreeCheckBudget&);
}  // namespace abcd::source
#endif  // ABCD_SOURCE_ARRAY_PATTERN_SURFACE_H_
