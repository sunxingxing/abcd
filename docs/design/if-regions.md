# Single-Arm If Regions

The common region tree now includes `RegionIf {header, body, join}`. Both the
normal acyclic/loop builders and the exception builder use the same CFG rule:
the header is conditional, its taken edge skips directly to the join, its
fallthrough enters the body, and the body has exactly that one incoming normal
edge and one normal successor to the join. The body must end in fallthrough
or an unconditional jump. The join remains in the current lexical sequence.
The exception owner additionally requires one unchanged catch-scope run for
header and body; its existing region certificate excludes exceptional entries
into their interior.

This is a basic single-entry/single-exit structural reduction, not a heuristic
based on instruction names or generated text. It does not require duplicating
instructions, a state-machine dispatcher or speculative removal of effects.
No dependency or imported implementation is involved. General multi-block
arms, if/else diamonds and branches across loop/catch boundaries remain on the
existing label-based path.

`CountRegionEntries` saturates at multiple entries rather than risking an
overflow. Its O(B+E) pass allocates an admitted O(B) byte-sized index; each
candidate check is O(1). Region construction visits each selected block once.
The node replaces existing block/label structure and stays within the existing
conservative node/depth budget.

## Printing and Provenance

The shared printer emits the header's original statements, then an if using
the exact negation of the admitted branch predicate. It does not reinterpret
the predicate as general truthiness or apply unproved NaN assumptions. The
branch keeps its span on the if condition. The body's terminal jump to the
join becomes a zero-width PC anchor; its fallthrough needs no generated break.
All body instructions keep their normal source spans.

Cross-block staged argument regions own their branch output. When an if node
intersects one, printing uses the equivalent original label layout so the
existing argument-consumption rules still apply. No staging boundary is moved.
Root-prefix extraction stops before the header's conditional terminator.

## Actual Progress

In pasteboard entry 8831 expansion, method 9386 (`PasteBoardBuilder`) now prints
a RegionIf with header 0, body 1 and join 2. Its shared template retains 36 PC
spans. Home's explicitly incomplete record output also contains native if
statements. Async child 9165 has no matching narrow region and retains its
398 spans unchanged.

Both production CLI targets build and read-only review found no concrete
defect. ABC-only output `build/pasteboard-if-regions-01` still publishes the
same three incomplete records; record completion and execution equivalence
are not claimed. Tests, JS execution and RSS measurement remain paused.
Deferred coverage includes alternate entries, loop headers/latches, catch
scope transitions, staged arguments, root prefixes, exact branch predicates,
unconditional-jump anchors, non-PC block order and low-memory budgets.
