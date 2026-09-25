# If Regions

The common region tree includes `RegionIf {header, body, join, alternate}`. Both the
normal acyclic/loop builders and the exception builder use the same CFG rule:
the header is conditional, its taken edge skips directly to the join, its
fallthrough enters the body, and each body has exactly one incoming normal
edge and either one normal successor to the join or a terminal return/throw.
A nonterminal arm ends in fallthrough or an unconditional jump. With an
alternate arm, the header's taken edge enters it instead of skipping directly
to the join. The join remains in the current lexical sequence.
The exception owner additionally requires one unchanged catch-scope run for
header and all arms; its existing region certificate excludes exceptional entries
into their interior.

This is a basic single-entry/single-exit structural reduction, not a heuristic
based on instruction names or generated text. Early return/throw is an explicit
terminal alternative, not a claim that all paths reach the join. It does not require duplicating
instructions, a state-machine dispatcher or speculative removal of effects.
No dependency or imported implementation is involved. General multi-block
arms and branches across loop/catch boundaries remain on the
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
statements. The initial single-arm rule did not match async child 9165.

The two-arm/terminal extension now structures 9165's header 1, returning body
2 and alternate 3, with continuation 4. Its shared template retains all 398
spans and six capture obligations; text decreases from 6256 to 6206 bytes.
Return/throw statements remain inside their original selected arm, including
the enclosing catch dispatch. Terminal arms do not require a fabricated edge
to the continuation. Staged argument fallback uses the equivalent two nested
labels when an alternate exists.

Both production CLI targets build and read-only review found no concrete
defect. ABC-only outputs `build/pasteboard-if-regions-01` and
`build/pasteboard-if-else-01` still publish the
same three incomplete records; record completion and execution equivalence
are not claimed. Tests, JS execution and RSS measurement remain paused.
Deferred coverage includes alternate entries, loop headers/latches, catch
scope transitions, staged arguments, root prefixes, exact branch predicates,
unconditional-jump anchors, terminal arms, two-arm diamonds, non-PC block order
and low-memory budgets.
