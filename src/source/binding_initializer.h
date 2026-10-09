#ifndef ABCD_SOURCE_BINDING_INITIALIZER_H_
#define ABCD_SOURCE_BINDING_INITIALIZER_H_
#include "tree_structure.h"
#include "expression_observation.h"
#include "primitive_value.h"
#include "expression_transport.h"
#include "binding_projection.h"
namespace abcd::source {
struct OriginalFlow;
struct LoopSyntax;
struct NullishSyntax;
struct BindingInitializer {
  SymbolId symbol;
  ExprId assignment;
  DeclarationKind declaration;
};
// An existing lexical initializer whose binding is removed or shares a slot.
// Its value still executes at the original statement, after all RHS effects;
// an absent target means GetValue only. This is a committed source projection.
struct DeclarationValue { ExprId value; std::optional<SymbolId> target; };
enum class DeclarationCompletionKind { kInitialize, kAssignUndefined };
// Evaluate an optional discarded RHS, then complete only on its normal path.
// Initialization and a write to an already initialized slot remain distinct.
struct DeclarationCompletion {
  SymbolId binding;
  std::optional<ExprId> evaluation;
  DeclarationKind declaration;
  DeclarationCompletionKind kind=DeclarationCompletionKind::kInitialize;
};
struct BindingInitializers {
  std::vector<std::optional<BindingInitializer>> statements;
  std::vector<std::uint8_t> omitted;
  // Statement indexed: common residual completion proof retains only its
  // mandatory entry evaluation, at the same scope/handler/occurrence.
  std::vector<std::optional<ExprId>> statement_values;
  // Statement indexed: the RHS moved to a real read. Its origin span belongs
  // to that destination; other omitted subtree origins stay at the boundary.
  std::vector<std::optional<ExprId>> forwarded_values;
  // LoopStmt indexed: first declarator in its existing ordered entry program.
  // Earlier effects remain before the loop, in their original handler/scope.
  std::vector<std::optional<std::uint32_t>> loop_declarations;
  // Immutable typed primitive values. The original AST and evaluation remain
  // untouched; each substituted read retains its own source range and grammar.
  std::vector<TreeLiteral> primitives;
  // SymbolId indexed: proven value identity in this pool, not an ExprId whose
  // original occurrence would be replayed. Includes lexical undefined.
  std::vector<std::optional<PrimitiveId>> literals;
  // ExprId indexed private read values, independent of all-write invariants.
  // Successful definitions and lexical availability are proved in common flow;
  // this projection never authorizes deleting the producer or its effects.
  std::vector<std::optional<PrimitiveId>> read_literals;
  // Final storage SymbolId indexed all-read substitution. Writes use Storage;
  // independent cleanup owns their evaluation, completion and binding removal.
  std::vector<std::optional<SymbolId>> read_bindings;
  // ExprId indexed identities proved for one actual completed read, including
  // a mutable entry cell's current value under shared copy availability. Late
  // ReadValues owns these after storage/motion; writes retain Storage.
  std::vector<std::optional<SymbolId>> read_sources;
  [[nodiscard]] SymbolId ReadBinding(ExprId expression,SymbolId binding) const {
    return BindingProjection{aliases,storage,read_bindings,read_sources}.Read(expression,binding);
  }
  [[nodiscard]] SymbolId ReadBinding(SymbolId binding) const {
    return BindingProjection{aliases,storage,read_bindings}.Read(binding);
  }
  [[nodiscard]] std::optional<PrimitiveId> ReadLiteral(ExprId expression,SymbolId binding) const {
    if(!read_literals.empty()&&read_literals[expression.value])return read_literals[expression.value];
    return literals[binding.value];
  }
  // SymbolId indexed: retained immutable producer storage for an elided copy.
  std::vector<std::optional<SymbolId>> aliases;
  // Mutable storage coalescing is separate from immutable value aliases.
  // Noninterfering groups share a retained binding after interference proof.
  std::vector<std::optional<SymbolId>> storage;
  // Statement indexed: a live empty-declaration value retained as a write to
  // shared initialized storage at the original occurrence, not a new binding.
  std::vector<std::optional<SymbolId>> undefined_stores;
  std::vector<std::optional<DeclarationValue>> declaration_values;
  std::vector<std::optional<DeclarationCompletion>> declaration_completions;
  [[nodiscard]] SymbolId Storage(SymbolId symbol) const {
    return BindingProjection{aliases,storage,read_bindings}.Storage(symbol);
  }
  // ExprId indexed: a redundant store projects to its RHS; a unique private
  // snapshot read can consume the original RHS after ordered lifetime proof.
  // A sequence with an inert discarded left projects to its right value; all
  // downstream analyses and the printer consume that same replacement.
  std::vector<std::optional<ExprId>> replacements;
  // ExprId indexed source occurrences removed by ordered snapshot transport.
  // Siblings remain; moved RHS origins belong to the retained read destination.
  std::vector<std::optional<ExpressionTransport>> transports;
  // Original ExprId indexed completion summary after proven store/copy
  // projection. Inert means normal, no hook/write/allocation/throw/suspension.
  std::vector<ExpressionEvaluation> evaluations;
  // Assignment PutValue is normal after RHS completion: writable initialized
  // declarative storage, with no property setter or unresolved binding lookup.
  // Describes retained assignments in the current projection; replaced nodes
  // have no PutValue fact. Recompute rather than accumulating earlier facts.
  std::vector<std::uint8_t> normal_stores;
  // Prefix counts of non-elided initializer roots, for constant-time ranges.
  std::vector<std::uint32_t> entry_counts;
  // Prefix counts over the canonical block child arena after statement elision.
  std::vector<std::uint32_t> child_counts;
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
// Eligible bindings form a sparse constant lattice over successful definitions:
// primitive values propagate through immutable copies, including implicit
// lexical initialization. Value substitution never removes writes/effects.
// Allocations, calls/getters, mutable values and captured/source bindings retain storage.
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
// Normal-completion result identities compose through assignment, prefix update,
// sequence and agreeing branch/short-circuit outcomes. A writable declarative
// binding already holding that exact result needs no second PutValue. Its RHS
// stays at the same occurrence, including hooks, throws, and suspension; member
// setters, postfix old values and unknown result identities retain their stores.
// Private single-use statement snapshots forward to the actual retained read
// after alias projection. The same control/exception/iteration activation, no
// intervening state observation, and equal lexical environment signatures prove
// execution count, effect/throw order and name resolution. Transparent scopes
// collapse; prospective native declarations remain part of the environment.
// Elided copy declarations are absent from the control/init event stream.
// Function/class naming follows the projected destination and retains GetValue.
// Strict resource contracts keep direct creations until a possible additional
// name-wrapper allocation is excluded; other contracts may forward them.
// RHS occurrences keep GetValue (not a member receiver or bare typeof lookup)
// and their original ownership/ranges, including when for syntax prints earlier.
// Ownership/copy census walks are linear; shared flow liveness additionally
// depends on sparse live facts and traversed fact-edge incidences (algorithms.md).
// Spelling-index construction adds bounded
// O(B log(B+1)) name comparisons, plus module sorting and compared text units.
// No per-symbol CFG or per-read ancestor walk, recursive walk or SSA merge.
[[nodiscard]] std::variant<BindingInitializers, TreeStructureError>
PlanBindingInitializers(const FunctionTree&, const LoopSyntax&, const NullishSyntax&,
    analysis::SemanticBudget budget = {},const OriginalFlow* original = nullptr);
}
#endif
