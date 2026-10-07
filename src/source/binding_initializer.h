#ifndef ABCD_SOURCE_BINDING_INITIALIZER_H_
#define ABCD_SOURCE_BINDING_INITIALIZER_H_
#include "tree_structure.h"
#include "expression_observation.h"
namespace abcd::source {
struct LoopSyntax;
struct NullishSyntax;
struct BindingInitializer {
  SymbolId symbol;
  ExprId assignment;
  DeclarationKind declaration;
};
struct BindingInitializers {
  std::vector<std::optional<BindingInitializer>> statements;
  std::vector<std::uint8_t> omitted;
  // LoopStmt indexed: first declarator in its existing ordered entry program.
  // Earlier effects remain before the loop, in their original handler/scope.
  std::vector<std::optional<std::uint32_t>> loop_declarations;
  // SymbolId indexed: original TreeLiteral occurrence for a proven immutable
  // primitive value. Stores and reads keep their own original source ranges.
  std::vector<std::optional<ExprId>> literals;
  // SymbolId indexed: retained immutable producer storage for an elided copy.
  std::vector<std::optional<SymbolId>> aliases;
  // ExprId indexed: a redundant private store projects to its original RHS
  // at the same evaluation occurrence, retaining its expression result.
  std::vector<std::optional<ExprId>> replacements;
  // Original ExprId indexed completion summary after proven store/copy
  // projection. Inert means normal, no hook/write/allocation/throw/suspension.
  std::vector<ExpressionEvaluation> evaluations;
  // Prefix counts of non-elided initializer roots, for constant-time ranges.
  std::vector<std::uint32_t> entry_counts;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Requires a structurally checked tree and its current loop/nullish projections.
// Native declarations at the original assignment occurrence. Empty let locals
// and private declarations use the same first-completion/confinement proof.
// Capture creation is an observation; escaping locals additionally retain the
// original scope activation (including cells shared across loop iterations).
// Source let stays let and retains storage; module aliases remain unchanged.
// The canonical AST, expressions and execution order remain unchanged;
// nonescaping syntax lifetimes may shrink through DFS lexical containment.
// Uncaptured loop-entry bindings may use native for let. The same
// first-store and confinement proof covers initializer/test/body/update and
// rejects observations after exit. Source/cell captures never change lifetime.
// Single-store eligible bindings form a sparse constant lattice: primitive
// TreeLiteral definitions propagate through immutable copies. Allocations,
// calls/getters, mutable values and captured/source bindings retain storage.
// Literal substitution keeps each read's origin and its actual parent grammar.
// Other immutable copies may share a retained producer after common lexical
// resolution at each remaining read; a required copy propagates upstream only
// where its RHS cannot resolve the root. No allocation/effect is replayed.
// A declaration completing before its confined observations may supply the
// same immutable root when no owned or unknown external write can alter it.
// Its original declaration kind, initializer, scope and NamedEvaluation stay;
// copies preceding completion/escaping activation or capturing mutation stay.
// Ordered expression completion uses shared child ownership and execution
// regions for short-circuit/choice/handler/loop activations. Embedded private
// stores project to their original pure RHS, preserving the observed result;
// that RHS remains a real read for the common root visibility proof.
// The same postorder census summarizes normal/no-hook/no-throw completion.
// Initialized binding reads and pure value operators can disappear from the
// discarded left of comma without changing the right's GetValue category.
// Unknown coercions/global/member reads and allocation/suspension stay owned.
// Ownership/copy walks are linear; spelling-index construction adds bounded
// O(B log(B+1)) name comparisons, plus module sorting and compared text units.
// No per-symbol CFG or per-read ancestor walk, recursive walk or SSA merge.
[[nodiscard]] std::variant<BindingInitializers, TreeStructureError>
PlanBindingInitializers(const FunctionTree&, const LoopSyntax&, const NullishSyntax&,
    analysis::SemanticBudget budget = {});
}
#endif
