# Certified lexical environment renewal

The phased source route retries only `EnvironmentFlowError::kAmbiguousJoin`.
Ordinary `AnalyzeEnvironmentSites` agreement and `EnvironmentIndex` physical
identity remain unchanged. This is a proof extension under the existing
`language_source_v10` contract, not a source-language premise.

## Pinned lowering and ownership

`ets_frontend/es2panda/compiler/core/envScope.cpp`,
`LoopEnvScope::CopyPerIterationCtx`, snapshots all lexical slots, departs the
old environment, allocates a replacement and restores the snapshot. In
`ir/statements/forUpdateStatement.cpp` this precedes the update expression.
For-of/in and while instead create their body environment on entry; their
existing successful source routes are retained.

The analysis owner is `analysis/environment_renewal.{h,cc}`. It consumes the
unchanged exception phase graph and its register SSA. It builds a separate
sparse environment-token SSA using the common IDF/rename implementation.
Tokens identify entry, successful allocation and edge-qualified phi states.
Allocation tokens retain their actual parent tokens. Phi edges use original
phase successor ordinals, including after resource-edge projection. Physical
allocation definitions are retained separately; a binding-family projection
never equates their runtime instances.

The certificate contains physical allocations and ancestry, current tokens,
renewal departures/allocations, slot read/save/restore witnesses, initialized
binding Commits and complete instruction/exception coverage. Register/phi copy
identity is solved with a directed three-point lattice; a mixed phi cannot
silently equate its distinct inputs. The copy corridor is checked over the
exception graph, allowing register copies and block splitting while refusing
interfering writes or callbacks.

## Body-reset projection

The carried slot value must be unobservable before the next initialization
Commit, including through captures, handlers, departures and loop exits.
A sparse cell-state SSA records resets separately from ordinary stores and
checks both directions:

- Every actual read/capture must see an initialized current generation.
- The declaring store must see only a reset, not a prior store. This proves
  exactly one declaration execution per generation, rather than static
  dominance alone; a back-edge that revisits initialization without renewing
  the environment refuses.

The source emitter uses the existing deferred-declaration mechanism to emit
`let x = init` at the actual initializer inside the loop body. The renewal
certificate authorizes crossing the old allocation's loop boundary; ordinary
deferral still forbids that move. Existing slot-stable copy operations retain
their checked IR/cell correspondence, and ordinary identity-write elimination
can omit their source self-assignments. No arbitrary effect is made pure.
Cross-layout slot transfers whose native storage correspondence does not close
remain typed `kSlotMap` refusals; coherent scope-wide renumbering is tested.

The initializer's operand/effect order and successful Commit remain owned by
the ordinary evaluation/operand/flow checks. Capture cells and complete-family
write checking retain their original obligations. Before-initialization
captures that cannot be proved safe remain refusals.

The renewed allocation can fail after departure. Only the existing source
resource-failure exclusion permits relocation to native binding activation.
The certificate retains excluded resource arcs; original recovery also keeps
its typed resource-completion coverage, including propagation to the caller.
No user-code exception is excluded. Raw `kObservable` analysis refuses this
projection. Exception input, local and capture analysis independently recheck
the supplied projected environment view against the immutable certificate.

Newly admitted cleanup/finally paths can expose over-approximated caught Hole
payloads. Only on this renewal retry, the source owner reuses the existing
activation-safe guarded catch-payload proof. It narrows certified throw-to-Catch
dependencies, never the payload globally; its existing independent verifier
and final-tree evidence remain required. No new finally recognition is added.

## Independent checks and costs

Phased import checks evidence identity and environment correspondence.
Working value-environment verification rebuilds the certified binding view;
cell plans, initialization and capture planning use that checked view.
Evaluation, operands, origins, tree flow, family writes and recovery replay
the immutable proof. Recovery additionally checks native declarations, their
initializer Commit correspondence and repeated lexical scope, then compares
trial binding units against that source scope rather than an unrelated initial
physical allocation. Removed output remains covered by cell/binding evidence
or explicitly excluded resource completions.

For N/E phase nodes/edges, U scalar/lexical uses and K actual
transfer/phi/capture incidences, the additional proof targets
`O((N+E+U+K) log(N+K+1))`. Cell-state SSA uses complete phi placement: pruned
per-binding liveness walks were measured to become quadratic and are not used
here. Shared dominators, sparse adjacency and monotone worklists avoid per-cell
CFG walks and dynamic-iteration enumeration. Vectors, retained retry inputs,
SSA/index grants and simultaneous scratch are charged before allocation;
pathological work/storage receives typed limit failures.

`tests/isa/environment_renewal_scale.py` compiles real O0 methods with
100/200/201/400/800 captured cells and bounds charged work per N log N. These
include wide lexical/environment formats. Mutation tests alter owned semantic
bodies/certificates, never input ABCs. They cover slot/source corruption,
interference, phi/parent/generation/Commit/coverage corruption, repeated
initialization, limits, renumbering, extra copies and block splitting.

## Deferred

Captured header-copy loops deliberately refuse with `kSourceScope`, retaining
the original ambiguity and join witness. Their native initial per-iteration
copy and update/continue lifecycle need a separate source-scope certificate.
There is no header-copy emission in this increment. Existing loop syntax for
uncaptured register counters is unchanged.

Nested iterator-close completion ports, including the service and DataDriver
records, are separate work. No iterator ownership or completion policy is
changed here.
