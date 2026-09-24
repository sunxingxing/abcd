# Exception Source Integration

## Current Boundary

The common semantic path supports explicit `THROW_PREF_NONE` as
`ThrowOperation`, with `ExceptionalCompletion::kAlwaysThrow`. The pinned
OpenHarmony-v7.0-Release interpreter handler in `interpreter-inl.cpp` passes
the accumulator to `SlowRuntimeStub::Throw` and jumps to exception dispatch.
The operation performs no coercion or normal register write. Its existing
register-read effect is checked by SSA Language-value verification; Hole and
unproven internal values are not emitted as JS exceptions.

Normal CFG construction already gives throw no successor. Region construction
therefore retains it as a terminal block, and `SourceThrow` prints the original
value with the instruction's original-PC provenance. Other THROW* opcodes are
not aliases: typed runtime error construction requires separate lifting.
The new semantic and exceptional-completion alternatives are appended to
preserve existing variant/enum indices.

This does not admit methods with exception tables. The common builder still
rejects those methods rather than silently dropping handlers. The async
normalizer only removes a proven single implicit catch/reject wrapper.

## Remaining Integration

Instruction-level edge selection is now available through
`LiftSemanticInstruction` and `ResolveExceptionEdge`. The former is shared by
the normal CFG/SSA builder, not a second opcode semantics implementation. The
latter queries the existing dispatch index and returns a typed handler target
or propagation to the caller, the instruction's pre-write register boundary,
and either `ThrownValue` or `PendingException`. Its lookup is O(log R), without
allocating an edge collection. The caller still owns CFG splitting, catch-entry
definitions and environment-state proof; this API does not supply them.

`abc_semantic --instructions` now publishes these associations even when the
whole method is not source-admitted. Unsupported lifting is an explicit error,
not an absent exceptional effect interpreted as `kImpossible`. Dispatch-index
failure is reported separately. Raw suspension/settlement instructions retain
their coroutine description but are not granted recovered async semantics.

1. Use the existing bounded `BuildExceptionDispatch` index, which preserves
   encoded handler priority. Do not sort overlapping try regions and thereby
   change selected handlers.
2. Extend the semantic CFG with typed exceptional edges from potentially or
   definitely throwing operations. A handler sees the throwing instruction's
   committed register/environment state, not its normal-path destination.
   The thrown accumulator value needs an explicit catch-entry definition.
3. Feed those edges and definitions through SSA and environment analysis.
   Normal and exceptional continuations must not share an optimistic value
   fact. Account for the expanded graph and actual phi/dependency counts.
4. Recover typed try/catch regions only after entry, exit and dispatch proofs.
   Catch bodies may mutate or inspect existing registers and lexical cells;
   they cannot be analyzed as unrelated standalone functions. Preserve throws
   from inside a catch and abrupt exits across enclosing regions.
5. Let async normalization remove only its outer compiler wrapper while
   retaining inner exception tables, rebased boundaries and original-PC
   mappings. Native await rejection must reach the same inner handler as the
   original resume/rethrow sequence. No rounding of deleted boundaries.
6. Lower these regions through the common AST/printer and then record owners.
   Keep current proven no-handler paths as a baseline, not a second exception
   semantics implementation.

Actual pasteboard method 9165 has two regions: `[40, 992)` dispatches to the
handler at 994 (length 5), and the outer async `[18, 1003)` dispatches to 1003
(length 5). This was inspected from the decoded production body in the
debugger. Removing both regions would suppress observable inner handling and
is not an acceptable shortcut.

## Verification Status

### Exception Register SSA

`RegisterUses::BuildSites` and `RegisterSsa::BuildSites` now accept a typed
register-effect program without a fabricated ABC body. They share validation,
pruned IDF placement and dominator renaming with the existing body API. That
body API still rejects encoded handlers and validates supplied semantic PCs;
its source admission has not been relaxed. Site APIs validate graph coverage,
slot/range bounds and topology before renaming, with the owner responsible for
semantic equivalence of the supplied effects and edges.

`ExceptionSsa` adapts each phase node to one effect site: Before reads inputs,
Commit defines only the normal destination, and Catch defines accumulator.
The adapter preserves successor identity and retains the typed exception graph
as provenance, rather than treating exceptional edges as normal completion.
Register SSA result instruction/phi indices refer to phase nodes, not original
ABC instructions; callers use the flow's node-to-instruction/PC mapping. This
does not yet prove exception value categories, environment restoration or
the relationship between a normal result's value rule and its Before reads.
Those are necessary before try/catch source admission.

Adapter storage is O(N+E) for phase nodes/arcs; it deducts its live arrays from
the SSA byte budget and charges its work to the shared method quota. The SSA
core retains its existing sparse IDF/dominator algorithms and budgeted
cross-binding work, not a new claim of globally linear SSA construction.

Both CLI targets compiled. ABC-only inspection of method 4945 produces 220
SSA values across its 407 phase nodes: handler PC 439 reads value 161, defined
by Catch node 406; the protected operation's normal Commit node 89 defines a
different value 39. Method 5165 produces 106 SSA values across 199 nodes and
a register-5 phi at node 78 with three distinct incoming edges. Artifacts are
`build/pasteboard-4945-exception-ssa.jsonl` and
`build/pasteboard-5165-exception-ssa.jsonl`. Inspection schema 2 reports
`register_ssa_complete: true` separately from `ssa_complete: false` and
`source_complete: false`. The all-record production survey retains
`[0,0,27,1,0,8,0,11,113,56,3]`; no additional complete record is claimed.
Tests and RSS remain paused. Deferred cases include failed destination writes,
handler/normal entry joins, catch register phis, unreachable commits, loops,
range effects, malformed site graphs and quota exhaustion.

### Phased Exception Graph

`BuildExceptionFlow` now consumes the shared raw lifting and dispatch index to
produce a typed instruction-level graph. Each instruction has Before and
Commit nodes; only Commit defines its normal register destination. Selected
exceptional paths go directly from Before to a shared Catch gateway, which
defines accumulator from the incoming exception before entering handler code.
Ordinary jumps to that code target Before directly, preserving their existing
accumulator. Always-throw instructions have no Before-to-Commit edge. Caller
propagation remains an explicit exception exit rather than a normal successor.
Unused Commit nodes can be unreachable; consumers must compute reachability.

This is graph infrastructure, not exception-aware SSA or source admission.
Environment restoration, catch-entry phi definitions and region integration
remain open. Raw unsupported coroutine operations still reject graph building;
owner-proven async normalization has not been routed through this API yet.

Storage reserves two nodes per instruction plus at most min(I,H) catch nodes.
Sparse catch gateways are indexed by target instruction; no per-register state
copies or I-by-H handler scans are introduced. The public byte budget admits
simultaneous normal CFG, dispatch, typed instructions, nodes and gateway index.
`graph_work` counts lifting/connection passes and lookup comparisons only;
normal-flow/dispatch preprocessing is byte-bounded but not covered by that work
counter. The header includes its R/H-dependent complexity, rather than claiming
the instruction-only graph cost is the whole pipeline cost.

The `--exception-flow` inspection mode reports phases, arcs, normal register
writes and catch accumulator definitions, with `ssa_complete: false` and
`source_complete: false`. ABC-only inspection of pasteboard method 4945
(`build/pasteboard-4945-exception-flow.jsonl`) produces 407 nodes for 203
instructions. Its instruction at PC 113 has normal successor Commit node 89
and exceptional successor Catch node 406. That gateway defines accumulator
and enters PC 439 at Before node 336, without visiting Commit node 89.
Both CLI targets compiled; tests and RSS remain paused. Deferred graph cases
include handler-sharing, normal entries into handler code, thrown-value
identity, conditional edges with equal targets, always-throw exits, unreachable
commits, multiple handlers, malformed input and byte/work limits.

After edge integration, both CLI targets compiled and the ABC-only all-record
survey `build/pasteboard-semantic-exception-edges.jsonl` retained the counts
below. In `build/pasteboard-9165-exception-edges.jsonl`, PC 30 selects outer
handler 1003; PC 46 selects inner handler 994; explicit throw PC 149 selects
994 with accumulator payload; the function creation at handler PC 994 selects
outer handler 1003 if it throws. Raw await PC 123 remains unsupported lifting,
not falsely nonthrowing. These observations establish edge selection only,
not exception-aware SSA or successful try/catch source recovery. Read-only
review found no concrete defect; tests and RSS remain paused.

Both CLI targets compiled after explicit-throw integration. ABC-only survey
`build/pasteboard-semantic-throw.jsonl` retains stage counts
`[0,0,27,1,0,8,0,11,113,56,3]`; this prerequisite does not increase complete
methods or records. Read-only review found no concrete defect. Tests and RSS
collection remain paused by request. Deferred regression cases include thrown
primitive/object identity, uninitialized/Hole refusal, malformed operands,
conditional throw/return paths, loop exits and original-PC spans. Exception
edge state, handler priority and nested async rejection require additional
coverage when those layers are implemented.
