# Copy Identity Elision

The shared AST now removes redundant accumulator reloads through exact SSA
copy identities, including cases where a saved register has other live uses.
This is a restricted local copy-propagation pass, not textual cleanup, heap
alias analysis or expression duplication. It reuses `CopyOrigins`, already
computed by the common AST builder; phi and entry identities remain distinct.

Within each normal block, only uninterrupted still-emitted nonthrowing scalar
slot copies participate. The AST source and destination must exactly match
the original semantic operation. An accumulator input establishes its current
identity. An accumulator destination is erased only when the incoming value
already has that identity. Register writes remain, including saves observed
by later instructions or handler phis. Any non-copy, no-op, fused instruction
or special-origin statement clears the state; no block boundary is crossed.

The pass runs after dead-copy and single-use adjacent-pair elision, before
array/class folding. Erased reloads become no-op statements with original PC
anchors. Original use counts remain conservative upper bounds for later folds.
No side effects, captured stores, exception edges or callee evaluations move.
No new allocation, dependency or premise is introduced. For I instructions
and R indexed reads, cost is O(I) with phased SSA lookup and O(I log R) with
normal lookup, plus O(1) scratch; it does not rescan a suffix per copy.

Both production CLIs build; read-only review found no concrete defect.
ABC-only export of unchanged pasteboard modules.abc to untracked
`build/pasteboard-copy-identity-01` retains eight incomplete app records and
the two host-constructor refusals at PC14. Across the eight source files,
text decreases from 11,993 lines / 205,085 bytes to 11,447 lines / 198,440
bytes. For example, Logger's four logging methods retain their receiver saves
while dropping immediate loads of the already-current accumulator value.

This is generation evidence only. Tests, generated JS execution and RSS
measurement remain paused. Deferred regressions include live saved-register
uses, intervening effects, different SSA versions of the same physical slot,
phi/handler boundaries, fused call staging, consecutive copy chains and
position anchors. All artifacts remain incomplete; source-style recovery,
full nested position mapping and semantic equivalence are not yet complete.

## Adjacent Self Copies

FoldStoredExpressions now recognizes an exact register-to-accumulator load
followed immediately by a save back to that same register. Instead of
retargeting the load into `r = r`, it emits two NoOperation anchors. Both the
typed source expression and original load must name that exact register;
original destination is accumulator, completion normal, and both instructions
have no normal, pre-throw or exceptional effect. The existing same-block,
unmerged-origin and exact sole-use accumulator proof remains mandatory,
including phi/handler consumers. No register or accumulator write is moved
across an instruction, branch or effect.

The saved register keeps its prior identical value. The accumulator has no
other observer, so its removed commit is unobservable. Original SSA definitions
and use counts are retained conservatively; later transformations do not gain
invented liveness evidence. The save retains previous_origin pointing to the
load's zero-width PC span. This works in statement and sequence output without
inspecting generated strings, and adds only constant checks to the existing
linear adjacent-pair pass, with no allocation or dependency.

Production builds and ABC-only `build/pasteboard-self-copies-01` export
succeeded. All six pages lose self-copy lines, totaling 69 lines / 767 bytes;
source totals become 5,935 lines / 112,679 bytes. Statuses remain eight
incomplete, two refused constructors at PC 14 and nine metadata-only records.
Partial maps retain 195 methods / 9,745 spans. Independent read-only review
found no concrete issue. Tests, JS execution and RSS remain paused.

Deferred regressions include different source/save registers, live accumulator
reads, handler phis, effectful separators, fused origins, mutable ABI slots,
sequence output and adjacent zero-width mapping. Native object construction
recovery and complete source equivalence remain unfinished.

## Argument Identity Chains

Ordered native argument admission can now follow an already-erased
register-to-accumulator-to-same-register pair from the call's SSA operand back
to its source assignment. It reuses the typed previous_origin copy certificate,
then independently checks original destinations, normal completion, pure
effects, exact SSA reads/writes and a single remaining use for every value,
including the starting producer and phi consumers. Different registers,
emitted copies, merged origins and any extra use retain the assignment.

The walk follows definitions backwards, bounded by the producer and call;
instruction positions strictly decrease. It does not rewrite SSA or subtract
counts. Ordered arguments have distinct register targets, so admitted chains
are disjoint. Cost is O(I log(I + U)) for I instructions and U indexed SSA reads,
with constant scratch and no new dependency. Expressions and effects stay in
place, and erased copy PC anchors remain unchanged. This closes the identity
gap between object initialization and the existing native argument printer,
without matching names, source text or sample identities.

Production abc_semantic/abc_structure builds and ABC-only export to
`build/pasteboard-argument-identities-01` succeeded. All 37 native object
arguments from the preceding integration now omit their private register
commit, for example Button.createWithLabel(({id: ..., params: [], ...})).
Output remains 5,509 lines and becomes 104,313 bytes (222 fewer); this is
source-expression recovery, not additional method coverage. Statuses remain
eight incomplete records, two host constructors refused at PC 14 and nine
metadata-only records. Partial maps retain 195 methods / 9,745 spans.

Home method 6016 PCs 24/28/30/34 map to the literal at bytes [19648,19748);
the erased self-copy PCs 39/41 retain zero-width anchors at byte 19748.
Read-only review found no concrete defect. Tests, JS execution and RSS remain
paused. Deferred regressions include repeated chains, distinct-register copies,
intermediate extra reads, phi uses, malformed/backward definitions, origin
conflicts and live register commits. Complete semantic equivalence is unproven.
