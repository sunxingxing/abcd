# Transitive capture preservation

## Contract

`ProveCapturePreservation` reads the actual ABC method targets and literal
arrays. It emits typed creation-PC certificates only when the created function
and every supported descendant cannot write an incoming ordinary lexical cell.
This is not evidence of initialization, value domain or object identity by
itself. `BuildLocalSsa` uses it only to exempt certified creation sites from the
capture barrier; constructor origin SSA still proves every super guard.

No source maps, evaluation source or printed-source inspection participate.
The certificates are ephemeral and belong to the unchanged body, file and
semantic profile used to construct them.

## Analysis

Resolved methods enter a deduplicated worklist. Each method passes shared typed
semantic lifting, register SSA and environment analysis. A captured lexical
store, current-environment access, exposed environment allocation result,
unsupported operation or unresolved child prevents certification. Object and
array buffers are admitted only with primitive literal tags, excluding hidden
method templates. Literal format validation remains the source writer's job.

The dependency graph uses reverse may-write propagation: unknown or writing
nodes poison every ancestor. Fully supported read-only cycles need no recursive
walk and are certified only after propagation finishes. This is a conventional
monotone graph fixed point; no new library or license is introduced. It is
deliberately more conservative than a cell-sensitive interprocedural analysis.

## Cost and Ownership

For M indexed methods, E discovered creation edges and R root creation sites,
graph storage is O(M + E + R). Sorting costs O(E log E); propagation costs
O(M log(E + 1) + E). Each reached method is decoded and analyzed once per
invocation, with the existing bounded SSA/environment costs added. Pool and
method lookup costs and instruction visits consume the work budget.

One child body and its IR/environment scratch are live at a time alongside the
graph. Allocation growth includes transient old/new buffers in admission;
scratch refunds do not refund persistent edges. Resource exhaustion is explicit,
never a silent read-only result. Current callable and semantic-body boundaries
independently reconstruct the proof; there is no cross-invocation cache claim.
Runtime scaling and process memory compliance have not been measured here.

## Pasteboard Checkpoint

The real Home constructor (method 5851) receives certificates for the primitive
object buffer at PC392 and function creation at PC398. Its complete range-super
origin proof now returns super PC84, including the previously rejected guard at
PC414. Debugger evidence is in
`build/pasteboard-capture-preservation-debug.txt`.

Both CLI targets build. The record manifest remains unchanged, so this is not
completion of Home or the corpus. Nested captured-this guards still need their
own initialized-object evidence before source emission can be completed.
Tests, executable equivalence checks and RSS tracking remain paused.

Deferred regressions: transitive writes, mixed read-only/writing cycles,
unresolved children, method-bearing buffers, environment escapes, stale or
duplicate PC certificates, historical profiles, and resource-limit boundaries.

## Source Capture Binding

`LocalAccessPlan` now retains the sorted instruction IDs of creation sites not
covered by validated same-body preservation certificates. The list includes
function, class, object-buffer and array-buffer creation, matching local load
admission. Normal source capture binding queries this list instead of the AST's
unfiltered previous-function hint when closure writes are unknown. A prior
barrier at or after the local environment allocation still rejects binding;
the current creation is excluded. Initialization and stronger defined-value
requirements remain separate checks. Phased exception capture binding retains
its own scope certificates and is unchanged.

Construction adds one charged linear instruction pass and O(C) retained storage
for C capture-capable sites, included in local-plan allocation admission.
Each binding uses an O(log C) predecessor query. No semantic assumption or
preservation certificate is inferred from successful source printing.

The production CLIs build and read-only review found no concrete defect. An
ABC-only Home entry 6100 inspection still refuses at PC389: capture depth 0,
slot 0 is initialized, but 15 unpreserved creation sites remain between its
allocation (instruction 0) and creation (instruction 138), ending at instruction
130. Consequently this change does not increase complete-record coverage.
The next proof work must address those sites, not bypass the remaining barrier.
Tests, generated-JS execution and RSS remain paused. Deferred regression cases
include preserved function/class/buffer sites, unknown barriers, same-block
reallocation in loops, forward initialization scopes and allocation limits.
