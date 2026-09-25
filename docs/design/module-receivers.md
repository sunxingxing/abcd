# Module Receiver Expressions

FoldBindingReceivers (originally FoldModuleReceivers) connects an already-proven module load to a staged member
call's receiver. It operates on shared typed AST nodes, not method names or
source fragments. Normal SSA, no catch region and no branch argument region
are required.

The load and property read must be in the same basic block. Only unmerged
NoOperation statements with impossible exceptions and no language effects, or
the immediately following certified module Hole guard, may separate them. The
module assignment must still target the accumulator and retain its unchanged
semantic ModuleLoad origin. Extra uses and effectful separators retain the old
form.

The initial direct-definition rule did not affect pasteboard: Logger's module
load at PC 12 is followed by a proven guard at PC 14, an elided receiver save
at PC 18 and reload at PC 20, then the property read at PC 22. HasReceiverUse
therefore traces only that typed, already-elided transport: the guard must
carry the module's previous_origin certificate and read its exact SSA value;
each adjacent save/reload pair must be pure and normally completing, with
exact source/target identities and sole-use definitions. After accounting for
the guard, the current value must have exactly one use at each transfer and
finally at the property read. No raw use count is mutated by this pass.

The module expression and original InstructionId become a BindingReceiver on
SourceStagedCall. The old assignment becomes a zero-width NoOperation. Native
module binding emission and all existing scope/import/export checks remain
mandatory. The live binding is evaluated exactly once, before getter lookup
and arguments, across no observable operation. This does not freeze imports
or assume that module exports are immutable.

Before emitting child arguments, the printer validates the original load PC's
span via its instruction distance and rebases it onto the actual module
receiver expression. An attached proven guard retains the same rebased span.
The property and call spans retain their existing owner.
Nested staged nodes stop the same-block candidate scan; candidates entirely
inside an argument interval use the same local proof. No text-based source-map
reconstruction or dedicated record output path is introduced.

Structural work is O(I); existing normal-SSA lookups across the disjoint
candidate intervals give O(I log(I + U)) worst-case time for U reads.
Scratch is O(1); the typed optional node is covered by
the existing sizeof-based AST admission. Independent read-only review found
no concrete correctness issue.

Tests, generated JS execution and RSS measurements remain paused. Deferred
regressions include mutable module bindings, throwing getters, extra SSA/phi
uses, effectful separators, nested arguments, malformed source distances,
binding-admission refusal, phased fallback, and exact generated positions.

## Production Evidence

Both production targets built successfully. The first ABC-only export,
`build/pasteboard-module-receivers-01`, was unchanged and led to inspection of
Logger method 4658's actual guard/transport chain. After adding that proof,
`build/pasteboard-module-receivers-02` totals 8,092 lines / 140,305 bytes versus
8,118 / 140,675 before this change. Six record sources changed. Logger's four
logging methods now contain direct module-member calls with native field/rest
arguments and no VM temporary declarations.

The exported map for method 4658 places PCs 12 and 14 on bytes [391,399), the
module identifier. PCs 22 and 70 cover bytes [391,452), the full call statement.
Global partial-map counts remain 195 methods / 9,745 spans. Record statuses
remain eight incomplete, two refused constructors at PC 14, and nine metadata.
The expanded proof received independent read-only review with no concrete
issue. No tests, generated JS execution or RSS checks were run; complete
behavioral equivalence remains unproven.

## General Binding Receivers

BindingReceiver now owns a ReceiverBinding variant of module, global, local
and captured reads. ReadBinding checks the original semantic operation:
ModuleLoad, read-name GlobalAccess with the same pool key, or LexicalLoad for
the AST's already-proven local/captured cell. It does not infer bindings from
register names or generated source. All existing single-use, same-block,
pure-transport, provenance and exception-region restrictions remain unchanged.
Module guard handling remains the original module-specific certificate.

The shared printer dispatches each variant back through Expression. Global
admission and lexical/capture initialization/domain obligations still apply;
captured receivers retain typed zero-width tokens. No mutable binding is
snapshotted early or reread late: the binding is evaluated once before its
property Reference and arguments, across no observable operation. Global
getters, property getters and exceptions therefore retain their order.

The moved load span retains full GeneratedPosition endpoints, including capture
cursors, before final binding insertion. Staged-call slot audits remain valid:
these four binding/cell alternatives contain no caller VM-register read; the
register alternative below is explicitly observed through Value.
The variant has no dynamic allocation. Its increased size is included in the
existing sizeof(SourceStatement) AST admission. The same disjoint-interval
algorithm retains O(I log(I + U)) time and O(1) extra scratch, with no new
dependency or licensing obligation.

Production abc_semantic/abc_structure builds and ABC-only export
`build/pasteboard-binding-receivers-01` succeeded. Six page records improve,
with direct Reflect.set, NavDestination.onReady and lexical-cell
observeComponentCreation2 calls. Totals are 6,476 lines / 119,430 bytes versus
6,618 / 121,555 in `build/pasteboard-unused-results-01`; common-record sizes
are unchanged. PageA method 7505 PC 12 now maps to [4981,4995), the directly
emitted lexical receiver. Statuses remain eight incomplete records, two
constructors refused at PC 14 and nine metadata records; partial maps retain
195 methods / 9,745 spans.

Independent read-only review found no concrete correctness issue. Tests,
generated JS execution and RSS checks remain paused. Deferred regressions add
global getter ordering, mutable lexical captures, stronger capture domains,
same-byte capture cursor relocation, and register/phi-observed receiver values.
Source equivalence and process memory-cap compliance remain unverified.

## Register Receivers

ReceiverBinding also admits a RegisterId when both the AST and its original
SemanticInstruction are the same register-to-accumulator copy. The original
destination must be the accumulator, completion normal, and effects pure.
Literal, unary, binary and property expressions do not pass this rule. The
existing same-block single-use/transport proof still licenses removing the
accumulator commit; no use count is changed and no emitted register write is
crossed. Already-eliminated pure NoOps do not mutate source register bindings.

Printing uses Value rather than a raw register name, so immutable this/formal
substitution and VM-slot observation remain active. Its spelling is a validated
identifier or owner-proven this, both valid member-access bases. Numeric literal
parenthesization is not broadened. Property Reference evaluation still happens
before argument effects, including arguments that later write this register.
The register id adds no dynamic allocation or extra scan; the same AST-size
admission and complexity bounds apply. Independent read-only review found no
concrete correctness issue. Tests, generated JS and RSS remain paused.

Production builds and ABC-only export `build/pasteboard-register-receivers-01`
succeeded. Five register receiver transfers disappeared across Home and
PasteBoard, including PasteBoard's addEntry calls on `$r6`. Totals are 6,471
lines / 119,355 bytes versus 6,476 / 119,430 previously. The other six source
records and their maps are byte-identical to `pasteboard-binding-receivers-01`.
No newly direct this call was observed in this corpus export. Statuses remain
eight incomplete records, two refused constructors at PC 14, nine metadata
records; partial maps remain 195 methods / 9,745 spans. Deferred cases include
later argument writes to the receiver slot, read-only formal substitution,
live phi consumers, and mismatched or effectful copy origins. Full behavioral
equivalence and memory-cap compliance remain unverified.
