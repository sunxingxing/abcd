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

### Instruction SSA View

The common statement AST now queries `InstructionSsa` through borrowed
`AstMethod`/`AstProof` inputs instead of directly interpreting normal SSA
origins. The existing public normal-method entry point constructs these views
without allocating or changing its proof premises. Private object tracking and
argument-region analysis consume the same method view.

For phased SSA, an O(I+N), byte-admitted index maps original instructions to
Before/Commit nodes. Reads come from Before; normal writes come from Commit.
`Definition` maps only Commit values back to original instruction IDs. Catch,
phi and method-entry values are not ordinary instruction definitions. Value
IDs, phi dependencies and slot identities remain those of the original SSA;
no normal-only reconstruction or cloned bytecode is involved. Borrowed read
and phi-input records retain native site/edge coordinates, which AST consumers
must not reinterpret as original instruction positions. All referenced owners
must remain alive and unchanged while these views are used.

The phased AST mode refuses normal-only receiver/property fusion and guarded
module-load fusion until their exceptional-state proofs exist. Normal mode
retains its previous behavior. Exception binding/local-initialization facts
still need owner-verified projection into instruction coordinates before the
new internal AST entry can be used for published catch-containing source.

Both CLI targets compiled. Production inspection schema 8 adds explicit
instruction-definition provenance: in method 4945, normal value 39 at Commit
node 89 maps to instruction 44, while Catch value 161 at node 406 has no
ordinary definition. Artifacts are
`build/pasteboard-4945-instruction-ssa.jsonl` and
`build/pasteboard-5165-instruction-ssa.jsonl`. The all-record report
`build/pasteboard-semantic-instruction-ssa.jsonl` is byte-identical to the
previous production report after routing the normal AST through this view.
Read-only review found no concrete defect. Tests and RSS remain paused; view
lifetime/move cases, malformed indexes, unreachable writes and budget limits
still require regression coverage. No additional complete record is claimed.

### Common Try/Catch Tree

`RegionTry` is now an alternative in the same `RegionNode` used by ordinary
source AST/printer paths. `BuildExceptionRegionTree` consumes the unchanged
control-region certificate and normal block topology. It emits existing block,
label and sequence nodes around try/catch nodes, not separately rebased method
segments. Every block is retained once except a proven compiler skip jump.
The current tree builder admits forward normal edges only; exception-loop
composition remains unsupported rather than bypassing loop proofs.

Protected/handler boundaries must align with real blocks. A gap is elided only
when it contains exactly one effect-free jump to the handler's end. A label
inside the protected body preserves normal exits to that gap, and no external
edge may target it. Other normal exits use enclosing common labels. A retained
skip instruction ID supplies its original source PC for a zero-width anchor.
Tree and scratch bounds cover all nodes, block-boundary maps and catch ranges;
construction is O(I+B+R+E), with bounded region nesting and no copied bytecode.

The common printer emits try/catch syntax, copies the caught value into the
existing accumulator binding and preserves the original handler statements.
It does not shadow accumulator with the catch parameter. `$caught` is reserved
against private registers, formals, current-function/class bindings and sorted
outer bindings; the outer check uses binary search, not a per-method full scan.
Catch installation extends the first handler instruction's generated span;
the skipped jump uses the existing zero-width statement-span mechanism. These
printer changes are compiled but are not yet an end-to-end source proof for
the exception samples.

Production inspection schema 7 builds this common tree for methods 4945, 5165,
6350 and 6733: respectively 6/11/6/11 blocks and 1/1/1/2 catch nodes. Artifacts
are `build/pasteboard-<method>-exception-tree.jsonl`. The original input-value
refusals remain in place. Exception SSA/proof still needs to be connected to
the common statement AST builder before these trees can publish source;
constructing a normal-only SSA over stripped handlers is not acceptable.
Read-only review prompted fixes for outer-binding lookup complexity and
resource-error classification. Tests and RSS remain paused, so executable
catch behavior, collision refusal and generated spans still await regression
verification. No additional complete source record is claimed.
Both CLI targets compiled after the review fixes. The all-record inspection
`build/pasteboard-semantic-exception-tree.jsonl` retains stage counts
`[0,0,27,1,0,8,0,11,113,56,3]`.

### Exception Control Regions

`BuildExceptionRegions` now proves a contiguous try/catch layout from decoded
tables and the unchanged phased flow. Regions retain encoded TryId identity;
a separate source order arranges outer regions before nested children without
changing dispatch priority. Each candidate has protected/handler instruction
ranges, parent scope and inherited outer dispatch. A child must fit entirely
within its parent's protected body or handler. Crossing scopes, typed/shared
handler shapes and non-boundary extents refuse rather than inventing nesting.
The gap between protected end and handler entry belongs to the outer scope.

The builder checks each actual exceptional target against this lexical nesting,
including throws from handlers and propagation to the caller. Normal Commit
edges may exit regions or enter a protected range at its start, but cannot
enter a handler from outside or a protected range in the middle. Both explicit
branches and fallthrough edges are checked. Handler length is a candidate
boundary, not sufficient evidence by itself. No instructions are duplicated,
rebased or replaced with synthetic returns. Ordinary branch/loop lowering,
catch accumulator installation, AST integration and source input proof remain
required; this certificate does not infer finally or permit source publication.

Construction uses O(R log R + R log I + I + N + E) time and O(I+R) storage,
including sorted region IDs, nesting stack and instruction scope index. Sorting,
boundary lookup, nesting and graph scans consume the explicit work budget; all
live vectors are byte-admitted. Region arrays and source order are retained for
the future common AST consumer.

Production ABC-only inspection schema 6 reports `exception_regions_proven`
independently of `source_inputs_proven` and `source_complete`. Pasteboard methods
4945, 5165 and 6350 each yield one region. Method 6733 yields two consecutive
regions: protected instruction ranges `[9,37)` and `[73,106)`, with handlers
`[38,73)` and `[107,142)`. Artifacts are
`build/pasteboard-<method>-exception-regions.jsonl`. All four methods still
fail the existing value-input check. Both CLI targets compiled; read-only
review found no concrete defect. Tests and RSS remain paused. Deferred cases
include nested catches, throws from handlers, shared/crossing scopes, normal
entries into handlers, try-interior entries, loops, equal starts, and budget
exhaustion. These observations do not add a complete source record.

### Exception Input Admission

`VerifyExceptionInputs` checks all reachable Before reads against converged
SSA value categories, including lexical stores, explicit throws and returns.
An unknown/internal value produces a typed failure with its original PC,
phase-node ID and SSA value ID. Environment presence supplies reachability,
not a claim that lexical cells are initialized. The API requires the same
unchanged flow/SSA/environment/value analyses. A successful input check is
conditional on owner premises and does not grant region, AST or source
publication proof.

`ExceptionLoadFact` carries a typed Commit-node identity for an owner-proven
normal lexical/module load result. The solver validates operation kind,
reachable SSA definition ownership and duplicates. Such facts seed only the
normal result; neither exception dispatch nor Catch value categories change.
No adjacent load/Hole-guard fusion is inferred. With handlers, changing the
throwing instruction can change the state observed by catch, so the ordinary
exception-free guard-pair shortcut is not a sufficient proof here. Captured
and local lexical initialization and module-owner facts must still be derived
and discharged by their respective analyses/owners.

The verifier consumes the immutable `AllReads()` span in node/operand order,
avoiding repeated per-node binary searches. It uses O(N+U) time, O(1) extra
storage and charges each node/read to its work budget. Load-premise scratch is
admitted by the existing value solver's bounded allocator and work quota.

ABC-only inspection schema 5 now distinguishes `source_inputs_proven` from
environment/value-flow completion and emits the first typed input refusal.
Without extra owner load/exception premises, pasteboard methods 4945, 5165,
6350 and 6733 refuse at PCs 36, 14, 8 and 24 respectively, with SSA values
194, 22, 19 and 26. Artifacts are
`build/pasteboard-<method>-exception-inputs.jsonl`. These are precise unresolved
inputs, not successful decompilations. Tests and RSS remain paused; malformed,
duplicate and unreachable load premises, mixed exception categories, range
reads, mismatched analyses and quota exhaustion need regression coverage.
After switching to sequential reads, method 4357 passes 35 input reads through
the same phased analysis (`build/pasteboard-4357-exception-inputs.jsonl`), while
5165 retains its PC 14 refusal. Both CLI targets compiled. Read-only review
confirmed the linear scan and premise boundaries. The all-record survey
`build/pasteboard-semantic-exception-inputs.jsonl` retains stage counts
`[0,0,27,1,0,8,0,11,113,56,3]`; no additional complete record is claimed.

### Exception Environment Topology

`AnalyzeEnvironmentSites` now shares the existing flat agreement lattice and
`EnvironmentIndex` between normal and exceptional paths. The normal body API
still rejects exception tables and resumes; it decodes lexical operations once
into typed sites. `AnalyzeExceptionEnvironments` adapts the phased graph:
lexical accesses bind at Before, push/pop occur at Commit, and Catch preserves
the incoming environment. All result indices in this adapter denote phase
nodes, not original instruction positions. This does not infer lexical cell
initialization, closure writes or source-level scope lifetimes.

The pinned `interpreter-inl.cpp` EXCEPTION handler (around line 3999) selects
the frame/PC and updates accumulator without replacing that frame's environment.
NEWLEXENV and named/wide variants (around line 4583) install the new environment
only after abrupt-completion checks. POPLEXENV explicitly selects the parent.
The adapter rejects before-throw environment writes, unknown normal environment
writes and suspension effects instead of silently treating them as identity.
Unequal environment identities at a join remain an explicit failure; no
arbitrary path selection or implicit catch-entry pop is allowed.

The O(N+E) adapter is byte-admitted while the shared analyzer runs, including
its site array and graph. Shared analysis retains O(N+E+(P+L)log D) time and
bounded ancestry storage. Accounting includes adapter storage and its pass;
the normal wrapper now has at most four instruction visits, including decoding.
The existing scaling assertion was updated to this bound but not executed.

ABC-only inspections of pasteboard methods 4945, 5165, 6350 and 6733 succeed
with schema 4 (`build/pasteboard-<method>-exception-environments.jsonl`). In
4945, catch node 406 retains captured depth zero and the lexical access at node
270 binds captured slot 1. In 6733, Commit node 1 allocates environment 0;
catch nodes 402 (PC 96) and 403 (PC 275) both retain that environment, and
access nodes 8 and 384 bind its slot 0. Reports distinguish
`environment_topology_complete: true` from `lexical_values_proven: false`;
`source_complete` and `ssa_complete` remain false. Both CLI targets compiled;
read-only review found no concrete defect. Tests and RSS remain paused.
Deferred regression cases include failed environment allocation, explicit pops
in handlers, branch-local environments, ambiguous catch joins, loop freshness,
malformed graphs and resource exhaustion. No additional source record is
claimed from topology evidence alone. The all-record production survey
`build/pasteboard-semantic-exception-environments.jsonl` retains stage counts
`[0,0,27,1,0,8,0,11,113,56,3]` after the shared-core refactor.

### Exception Value Flow

`ExceptionValueFlow` now uses the same typed register-value rules and sparse
worklist as `RegisterValueFlow`. The shared solver lives in the private
`value_propagation.h`; it does not interpret opcodes. Normal Commit definitions
depend on their original Before operands. Catch definitions join reachable
exception payloads, and explicit throws preserve their source SSA categories.
Pending runtime/user-code exceptions default to Unknown. Only an explicit owner
premise may classify pending exceptions as Language; that premise never changes
explicit-throw facts. Entry facts likewise remain owner-supplied assumptions.

The adapter validates SSA definition slots and operand/dependency bounds.
Its phase lookup and reachability passes are O(N+E); shared finite-height
propagation is O(V+D) for SSA values and actual dependency edges. Incremental
phi joins avoid repeated fan-in scans. Scratch arrays and the simultaneously
live published fact array are byte-admitted; work consumes the shared quota.
This establishes register value propagation, not lexical/environment state,
module/capture load proof, input validation or try/catch source admission.

Both CLI targets compiled after integration. ABC-only inspections in
`build/pasteboard-4945-exception-values.jsonl` and
`build/pasteboard-5165-exception-values.jsonl` report schema 3. In method 4945,
normal Commit value 39 is Language, while Catch value 161 remains Unknown.
In method 5165, register-5 phi value 15 retains String across three incoming
definitions. The inspector explicitly reports `requires_language_abi: true`,
`pending_exception_values_proven: false`, `value_flow_complete: true`, and
keeps `ssa_complete` and `source_complete` false. The all-record survey
`build/pasteboard-semantic-exception-values.jsonl` retains stage counts
`[0,0,27,1,0,8,0,11,113,56,3]`. No additional complete record is claimed.
Read-only review found no concrete correctness defect. Tests and RSS remain
paused; explicit-throw joins, unreachable exceptions, loops, invalid SSA and
byte/work exhaustion still require regression coverage when testing resumes.

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
does not itself prove exception value categories or environment restoration.
The value-flow layer above now connects normal result rules to Before reads;
environment topology is now analyzed above, while lexical cell/value validation
remains necessary before try/catch source admission.

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

The graph itself does not grant source admission. Register SSA and value flow
are described above; lexical values and region integration remain open.
Raw unsupported coroutine operations still reject graph building;
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
