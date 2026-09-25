# Used VM Slot Declarations

The common printer extends its whole-body VmSlotUse certificate with a typed
RegisterUse vector and an accumulator-use flag. The vector records references
in actual bound scalar output, including assignment targets. It is not a
substitute for SSA liveness and does not remove observable assignments.

Only functions passing the exhaustive bound-read audit receive precise facts.
After immutable entry substitution, native-this/formal reads need no VM local;
other register reads and writes mark their original slot. Observation continues
after the first required slot so mixed bodies receive complete information.
Unknown/opaque and legacy paths retain all previous declarations.

FunctionWriter verifies the vector length against the real frame and emits only
used VM declarations in precise bodies. ABI initialization remains attached to
each retained slot. The declaration decision precedes map/capture rebasing.
Whole-slot-free methods keep the previous zero-prologue behavior.

The register vector costs O(R) retained bytes and is admitted by capacity before
any output allocation. Tracking is O(1) per scalar operand and declaration
emission is O(R); no per-register AST scans are introduced. Printed templates
move these facts; BindSourceTemplate admits and charges the output copy before
allocation. Capture-prefix budgeting includes the retained vector capacity.

Read-only review found no concrete correctness issue. Tests, generated JS
execution and RSS measurements remain paused. Deferred regressions include
accumulator-only/register-only bodies, target-only references, retained ABI
initializers, immutable formals, Unknown fallback, template copies, mismatched
frame dimensions, tight budgets and 200/201 plus geometric scaling. Full
source equivalence and per-register precision in opaque methods remain open.

## Production Evidence

Production targets `abc_semantic` and `abc_structure` built successfully. The
ABC-only pasteboard export in `build/pasteboard-used-slots-01` contains 9,003
source lines / 155,013 bytes, down from 9,339 lines / 159,245 bytes in
`build/pasteboard-slot-free-01`. The 336 removed declaration lines are spread
across six page records; CommonConstants and Logger are unchanged.

Coverage remains eight incomplete source records, two refused constructors
(entities 4767 and 5246, both at PC 14), and nine metadata records. Method
position maps still contain 195 distinct methods and 9,745 spans. These counts
are structural export evidence, not executable-source or equivalence evidence.
Tests, generated JS execution and RSS measurements were not run.

## Expression Read Audit

The exhaustive audit also admits GlobalExpression, GlobalObjectLoad,
BufferedObjectCreation, BufferedArrayCreation, AwaitValue and RestCopy.
Global loads consume names/realm requirements, not caller VM slots. Buffered
literal writers consume constant-pool data and retain their existing literal
admission restrictions. AwaitValue reads its operand through Value, which binds
and observes the actual slot. RestCopy prints the owner-validated native rest
formal: its isolated scalar writer's Accumulator alternative is a formatting
input, not a read of the caller accumulator. Targets remain observed separately.

This does not authorize new global bindings, async staging, buffer contents or
rest allocations. Existing printer checks remain mandatory. Class/function
creation, captured expressions, super calls, branch argument and catch regions
were outside this initial audit; later sections and the linked audit documents
record subsequent admission. No generated text is inspected for slot references.

After successful production builds, the ABC-only export in
`build/pasteboard-expression-slots-01` contains 8,938 lines / 154,233 bytes,
65 fewer lines than `build/pasteboard-used-slots-01`. Record statuses remain
eight incomplete, two refused and nine metadata; partial method maps remain
195 methods / 9,745 spans. Independent read-only review found no concrete
correctness issue. Tests, JS execution and RSS checks remain paused. Deferred
regressions must exercise operand/target-only await use, rest-formal identity,
fresh literal allocation, global binding refusal and conservative catch/staging
fallback; export counts do not establish those behavioral properties.

## Staged Call Reads

SourceStagedCall is admitted by the exhaustive operation audit. Its callee,
result and arguments pass through Slot/Value; its child statements remain in
the flat AST scan, including nested intervals. This must never become an audit
that skips children after visiting the interval owner. Comma-mode assignments
retain and observe their actual targets, while fallback arrow IIFEs preserve
lexical this and formal bindings. Entry-copy suppression retains zero-width PC
anchors. Catch regions remain excluded; branch argument regions and native
array owners now have the additional audit in `region-slot-audit.md`.

Production targets built successfully. ABC-only export
`build/pasteboard-staged-slots-01` contains 8,457 lines / 147,149 bytes, down
270 lines from `build/pasteboard-argument-sequences-01`; Logger accounts for
40 lines. Eight incomplete records, two refused constructors at PC 14, nine
metadata records and partial maps with 195 methods / 9,745 spans remain
unchanged. Independent read-only review found no concrete correctness issue.
Tests, JS execution and RSS remain paused. Deferred regressions include nested
staged intervals with bound entry reads, comma-mode target-only registers,
fallback arrow lexical bindings and zero-width copy anchors. Rest-formal
transport and effectful argument temporaries are not eliminated by this audit.

## Range Super Reads

The exhaustive expression audit now includes SuperRange. Its shared printer
checks the range and derived-constructor authority, then reads every explicit
argument through Value, in order. The result target is observed by its owning
assignment. Native super syntax supplies current-function and new.target
semantics; it does not read their synthetic ABI register names. Any other
retained use of those registers still records them independently.

This enables precise declaration census and existing immutable entry-copy
elision in admitted range-derived constructors. The whole-constructor proof,
guard ownership, single-super restrictions, private VM this initialization and
normal/exception effects are unchanged. In particular, no substitution of
native this before super or widening of spread-super admission is authorized.
Identifier-only class creation and unsupported member protocols retain their
conservative fallback. Removed entry copies retain zero-width PC anchors.

The audit remains a single O(I) traversal; each emitted argument adds constant
slot bookkeeping to its existing emission work. No new storage is introduced
beyond the existing bounded register-use and entry-binding tables. Deferred
regressions include zero/multiple arguments, argument-only registers, writable
and immutable parameter copies, explicit function/new.target uses, pre-super
VM this, thrown super calls, narrow/wide encodings and refused spread calls.

Both production targets built. ABC-only export
`build/pasteboard-super-slot-audit-20260925` yields 3,899 lines / 86,780 bytes,
81 lines / 1,049 bytes fewer than the neutral-parameter-aliases export. All six
page constructors lose unused declarations or immutable entry transport. PageA
method 7201 PCs 9/12/15 and 24 now retain zero-width anchors; super PC 79 and
its fused store PC 83 both map to [2602,2640), the retained super assignment.
Partial maps retain 195 methods / 9,745 spans; record statuses remain eight
incomplete, two host constructors refused at PC 14 and nine metadata-only.
Read-only review found no concrete defect. Tests, generated JS execution and
RSS remain paused. These observations do not establish full equivalence,
complete source-variable recovery or structural gate completion.
