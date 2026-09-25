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
