# Direct Register Assignments

The common AST redirects a single-use accumulator expression directly into
the private register saved by the immediately following instruction. The RHS
remains at its original PC; its register destination commits only on normal
completion. The old save becomes a no-op sharing the producer's generated
span through `previous_origin`.

Both instructions must be in the same normal block. The producer must define
acc, and the save must still be a nonthrowing acc-to-register scalar copy in
both typed IR and source AST, with the same register destination. Exact SSA
identity must connect them; the producer has only that save use, including
exceptional phi inputs. No fused or special-origin statement participates.
This preserves the old destination if RHS evaluation throws and admits no
other observer of the removed acc commit.

## Pass Order

The pass runs after dead-copy and single-use adjacent save/reload removal,
but before identity-based reload elimination. This order is mandatory: a
previously elided reload could rely on the acc definition this pass removes.
The later identity pass rejects the changed target as a mismatch with original
IR, preventing an invalid acc-preservation inference. Original use counts
remain conservative; they are not recomputed by scanning source text.

Class, buffered-object, buffered-array and empty-array creation retain their
acc/save shape for subsequent aggregate folding. Other expression-owned
creation coordinates and capture obligations are unchanged. Existing array
element and call argument regions consume the resulting typed assignments.

For I instructions and R indexed reads, cost is O(I) with phased lookup and
O(I log R) with normal SSA lookup. Scratch is O(1), no additional heap table
or statement payload is allocated, and no per-variable graph walk occurs.

## Evidence

Both production CLIs build; read-only review found no concrete defect.
ABC-only export of unchanged pasteboard modules.abc to untracked
`build/pasteboard-register-assignments-01` retains eight incomplete app records
and the two rejected host constructors at PC14. Compared with
`pasteboard-member-access-01`, combined source decreases from 11,422 lines /
194,770 bytes to 10,463 lines / 181,937 bytes. Logger emits direct assignments
such as `$r7 = ($acc).domain` and `$r3 = args` through the shared AST.

Tests, generated JS execution and RSS measurement remain paused. Deferred
regressions include throwing RHSs and handler snapshots, destination aliases,
copy chains and pass ordering, aggregate folding, staged calls, captures,
source-span aliases and output budgets. All artifacts remain incomplete;
this does not establish full source-style recovery or semantic equivalence.
