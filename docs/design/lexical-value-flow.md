# Local Lexical Value Categories

## Decision

Extend the existing finite-height register value solver with sparse local cell
nodes. This continues the approved lexical recovery work, without claiming
that value categories establish a last store or authorize code motion.

Alternatives are per-cell reaching-definition CFG scans (rejected because of
cross-cell quadratic work), or immediate source-variable substitution (rejected
because it would conflate allocation instances, Hole, and external mutation).
The chosen joint graph handles register/cell cycles without repeated pass
invocation or treating a lexical load as an independent unknown value.

## Contract

`RegisterValueFlow::BuildWithLexicals` additionally requires environment flow
and escape results for the SAME unchanged body/frame/SSA. Only actually accessed
local cells whose owner is proven nonescaping participate. Escaped and captured
cells remain unknown. This inherits the escape analysis's ordinary-reference,
non-resuming, exception-free model, including rejection of opaque numeric
representation transformations. Entry facts are still caller-proven assumptions.

For each participating cell, seed Hole and join every explicit store's SSA
input. A lexical load joins that cell's category set. This is deliberately
flow-insensitive over cells and all dynamic instances of an allocation site:
even a definitely initialized load retains Hole. Allocation never reuses a
previous instance's value in this model: Hole is always included. No reaching
store, identity equality, lifetime, or instruction elimination is inferred.
Calls cannot externally mutate these cells under the nonescape precondition.
Closure creation conservatively escapes its environment in the prerequisite.
The pinned runtime evidence for fresh Hole slots and explicit guard semantics
is recorded in `../reference/lexical-operations.md`; in particular,
`ecmascript/object_factory.cpp:2708` initializes `NewLexicalEnv` storage to Hole.

GuardedReads can subsequently exclude Hole from a particular SSA use after a
dominating completed guard. That does not refine the cell globally or make an
unrelated later lexical load initialized. Environment and other internal values
must remain in the result when stored; absence of Hole alone is not a language
value proof. The original Build entry point retains its behavior.

## Cost and Ownership

Sort/deduplicate actually accessed eligible bindings, not declared slot counts.
Use a private typed cell ID distinct from SSA IDs. The shared solver uses typed
node IDs and CSR edges: store value -> cell, cell -> load value. Every node grows
at most four times; each edge is visited at most four times. No cell scans the
CFG and no pass is rerun per cell or until convergence of whole-method passes.

For L lexical accesses and C eligible cells, the additional indexed extraction
cost is O(L log(2+L+V+I)), with O(C+L) graph storage beyond the existing register
solver. Register graph extraction retains its documented indexed-query cost.
Joint solving is O(V+C+D), where D includes register and lexical dependency
edges and the fixed lattice height is four. Input body/SSA/environment/escape
memory is caller-admitted. PMR scratch, its growth overlap and immutable owned
register-fact publication are admitted together. No temporary cell graph is
borrowed by the result. This is not the asynchronous method-task service.

## Implementation and Verification Plan

1. Add an initially delegating BuildWithLexicals API and a failing local load
   regression expecting Hole | Language instead of Unknown. Observe the failure.
2. Add a private lexical index in `src/analysis/lexical_value_index.{h,cc}`;
   validate references and collect sparse typed load/store edges under shared
   quota and PMR accounting. Extend `register_value_flow.cc` to solve jointly.
3. Add dedicated unit cases for uninitialized cells, stores before/after loads,
   loops, nested environments, escaped/captured boundaries, deprecated/wide
   encodings, copies/cell cycles, guard integration, budgets and owned results.
4. Add a small independent full-sweep oracle and geometric 200/201/4096/65536
   stress fixtures in tests/support. Run the full Debug suite, sanitizer target,
   isolated process under 1000000000-byte address-space cap, and review.
5. Record measured outcomes and limitations, commit the coherent change, and
   retain the complete per-record source objective. This stage alone changes
   no source coverage and does not warrant a redundant corpus source scan.
