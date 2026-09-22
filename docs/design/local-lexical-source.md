# Straight-Line Local Lexical Source

## Scope and Decision

Produce real JavaScript local declarations, stores and loads for straight-line
methods with ordinary local lexical environments. This is an increment toward
full per-record source, not a substitute for closures, branching environments,
TDZ failures, function wrappers or complete records.

Use an exact sequential initialization proof for this path. General cell SSA
and runtime sentinel emulation are alternatives, but neither is needed for
these methods; do not fake Hole as undefined or introduce an opcode dispatcher.
The existing joint value solver remains useful for general CFG recovery.

## Proof

Inputs are validated body/frame and the same ordinary EnvironmentFlow. Reject
handlers, jumps, branches, suspension, early terminators and missing terminal
return/throw. Registers start as caller-proven language values (the existing
source API contract); acc starts Hole. Sparse cells are keyed by local owner
and slot and start uninitialized. Captured accesses are rejected.

Traverse once. A store must consume a language value and marks its cell
initialized; a load requires that mark and produces a language value. Different
owners never share a variable. There is no backedge or repeated allocation.
Push/pop/current-environment and Hole loads update abstract state but need not
emit a runtime object/sentinel. Copies of non-language abstract values update
state and are omitted. Any observable use of those values is rejected. Function
creation is explicitly rejected because it could capture the implicit current
environment without reading a register. The allowed generic register effects
and value rules remain typed and reuse the analysis implementations.
Pop/current-environment are accepted only for a known local current environment.
Captured pop is not certified by a static depth number. Named allocations are
rejected because their scope literal-array lookup is actual runtime work, not a
conditional diagnostic lookup that can be skipped on a proven successful guard.

Every generic instruction's reads must be language-only. Result classification
uses existing conservative rules: unknown numeric/call/property outputs cannot
subsequently be used without proof. Dead results may still be emitted to retain
instruction effects. This is not a new proof of scalar operation equivalence;
generic emission retains the existing reviewed instruction contracts.

A Hole guard is omitted ONLY when its tested value is already language-only.
Do not inspect the conditional name register/string pool on that path. Guards
that may throw, uninitialized loads and internal values at opaque consumers are
typed failures, not invented source. Removing unobservable internal allocations
uses ordinary language-observable semantics, not equivalence of GC/OOM timing or
debugger introspection. Runtime evidence: `../reference/lexical-operations.md`
and `../reference/hole-guards.md` at the pinned release.

## Representation and Resources

An owned LinearLexicals plan has one typed statement per instruction (keep,
omit, local load, local store) and sorted actually accessed cell keys. Source
declares `$lex<environment>_<slot>` once per cell and emits assignments at their
original positions. Reject input binding names beginning `$lex` to prevent
capture. All bindings are function-local; sidecar source is never an input.
Register/acc bindings must be private temporaries inaccessible to called code,
matching VM registers rather than caller-created closures over JS locals.
Generic statements share the existing linear instruction renderer behind a
private, proof-required boundary. Two-pass streaming preflight prevents partial
semantic/budget output; IO or changing files still require atomic publication.

For I instructions, L accesses, C cells, R registers and U actual register reads:
plan construction is O(I+R+U+L log(2+L)), plus indexed environment binding work;
memory O(I+R+C+L). No declared-slot expansion, per-cell CFG scan or growing source
string buffer. Shared work quota and PMR count scratch/growth/output copies.
The source wrapper admits graph, environment flow and plan under one analysis
allowance. Caller separately admits body, names, transient strings and output.

## Implementation Plan

1. Observe a failing source regression for NEWLEXENV, store, load, known-safe
   Hole guard, return. Initially delegate the new API to WriteLinearBody.
2. Add `analysis/linear_lexicals.h` and its implementation, reuse environment,
   effect and value rules, and test typed refusal and resource ownership.
3. Add `source/local_lexical_body.h/.cc`; expose only a private single-statement
   rendering boundary from linear_body.cc. Test exact JS and no-prefix failure.
4. Execute generated JS in Node with side-effect/identity cases, old/new encodings,
   nested slots and invalid inputs. Test 200/201/geometric sparse scaling.
5. Add success-only fallback to source inspection; preserve original obstacle
   reporting when neither source path succeeds. Run Debug, sanitizer, isolated
   RSS and full-corpus source probe under 1000000000-byte cap. Independently
   review, record measured coverage delta, commit; keep the full goal active.
