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
creation, captured expressions, super calls, staged arguments and catch regions
remain outside this audit. No generated text is inspected for slot references.

After successful production builds, the ABC-only export in
`build/pasteboard-expression-slots-01` contains 8,938 lines / 154,233 bytes,
65 fewer lines than `build/pasteboard-used-slots-01`. Record statuses remain
eight incomplete, two refused and nine metadata; partial method maps remain
195 methods / 9,745 spans. Independent read-only review found no concrete
correctness issue. Tests, JS execution and RSS checks remain paused. Deferred
regressions must exercise operand/target-only await use, rest-formal identity,
fresh literal allocation, global binding refusal and conservative catch/staging
fallback; export counts do not establish those behavioral properties.
