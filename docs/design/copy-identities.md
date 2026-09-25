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
