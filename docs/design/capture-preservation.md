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

Both DEFINEFUNC and DEFINEMETHOD enter this graph. The pinned interpreter's
DEFINEMETHOD handlers pass the current frame environment to DefineMethod;
their accumulator home object does not change lexical capture ownership.
Function/home-object combinations are checked before resolving a target.
Ordinary class creation contributes its constructor plus every method decoded
from the class buffer, including accessors and generator templates. Literal
targets require actual method membership and decoded non-sendable attributes.
Missing targets, malformed buffers and unsupported children never certify a
class. A root stores a range of dependencies and emits one PC certificate only
when all targets are read-only. Nested class dependencies use the same reverse
propagation graph; source syntax/descriptor validation remains independent.

Class literal entries and decoded properties are scratch allocations charged
while target/edge storage grows. Root target ranges use geometrically grown,
budgeted storage. Add O(L + T log M) inspection work for L class literal entries
and T referenced templates, plus O(T) root target storage. Unknown descendants
may retain conservative queued work, but cannot leave a successful empty root.

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
130. That checkpoint preceded class/method dependency support.

With class/method dependency support, the same ABC-only inspection retains only
instructions 115 and 118 (PC305/317, targets 6350/6733). The class site and the
other 12 method sites now receive certificates. Both remaining methods fail
the normal semantic builder with `SemanticError::kExceptionRegions`; this is
not evidence of a captured write. The next work is exception-aware inspection,
not ignoring catches. Home still refuses at PC389, so complete-record coverage
has not increased. Both production CLIs compile and read-only review found no
concrete defect in the dependency extension.
Tests, generated-JS execution and RSS remain paused. Deferred regression cases
include preserved function/class/buffer sites, unknown barriers, same-block
reallocation in loops, forward initialization scopes and allocation limits.
Class-specific deferred cases include writing getters/setters, unknown literal
targets, read-only/writing dependency cycles through classes, duplicate method
templates, partial root failure and literal/edge growth resource exhaustion.

## Exception-Aware Inspection

Bodies with try regions now use the existing BuildExceptionFlow, ExceptionSsa
and AnalyzeExceptionEnvironments pipeline instead of the normal-only builder.
The original exception table is retained. Both pipelines share captured-store,
environment-result escape and descendant dependency checks. Binding and SSA
definition indices in this pipeline are phase-node IDs: they are explicitly
mapped to original instructions. Catch definitions are pending exceptions, not
allocation results. Thus a handler that writes an incoming lexical cell blocks
the same certificate as a normal-path write, and a phi carrying an allocated
environment remains an escape. Unsupported suspension and ambiguous environment
joins still refuse certification. No exception value-domain assumption is
needed to prove absence of captured writes; executable source admission retains
its independent checks and versioned source contract.

The exception graph exposes its charged lifting/connection work through
`work_units`. Capture inspection deducts graph work, SSA quota consumption and
environment visits from the shared budget, with simultaneously live graph,
SSA and environment storage admitted separately. Existing byte-bounded normal
flow/dispatch validation is not included in that graph work counter. Complexity
is the established exception CFG/SSA/environment cost plus one linear inspection
of bindings, SSA reads/phi inputs and instructions; no per-capture graph walk.

ABC-only generation of Home entry 6100 now succeeds through PC419, including
the formerly blocked route callback at PC389. Artifact:
`build/pasteboard-home-exception-preservation.json`, 48,935 source characters
and 150 entry-method PC spans. This is not execution-equivalence evidence or
a claim of per-descendant position coverage. The CLI still explicitly reports
`source_complete: false` and `executable: false`; record-file publication and
readable source-style recovery are not finished. ProgressBar entry 9915 still
refuses at class PC264 on its unresolved nested async path.

Both production CLIs build; read-only review found no concrete defect. Tests,
generated-JS execution and RSS remain paused. Deferred cases include writes
only in handlers, environment allocation failure versus normal commit, handler
phi escape, catch/normal shared targets, nested exceptions, ambiguous joins,
unknown children in handlers, suspension and each stage's resource exhaustion.
