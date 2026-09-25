# Acyclic Source Block Layout

The common acyclic region builder uses iterative depth-first reverse postorder
after the existing Kahn pass has certified a complete reachable DAG. Kahn's
FIFO can interleave independent arms when both become ready; this disrupts
local region reduction even when the original graph has nested diamonds.
Reverse postorder keeps a DFS subtree together. Taken-first visitation prefers
fallthrough-first source layout; the existing terminal-arm and simple-diamond
normalizations then run unchanged.

This is the standard DFS reverse-postorder DAG ordering, implemented in-tree
with no dependency. It is not a new permission to move effects across edges.
All blocks and original edges remain represented, instruction order within
each block is unchanged, and the existing complete forward-edge check runs
before region construction. Branches and labels continue to select the original
successors. Cycles retain the existing loop-region fallback; exception regions
and shared SSA/analysis ordering are unchanged. Irreducible or unsupported
structures are not made admissible by layout alone.

The already admitted order vector is reused as the postorder output, the
adjacent-arm scratch vector as the explicit DFS stack, and order_positions as
visited/next-edge cursors. Kahn validation ensures valid bounded normal edges
and no backedges. Every vertex is pushed once and each edge visited once.
The stack is empty and cursors are cleared before terminal-arm normalization.
O(B+E) additional work, O(B) existing scratch, no recursive traversal, extra
allocation, sorting or repeated subtree scans. The existing region depth and
capacity guards remain. This does not itself introduce nested region nodes;
local reductions and labels still determine the recovered syntax.

Tests, generated JS execution and RSS are paused. Deferred regressions include
nested/shared diamonds, direct joins, parallel edges, terminal arms, multiple
joins, malformed/resume edges, unreachable blocks, loops, exception fallback,
position maps, resource/depth limits and scaling. Output inspection is not a
behavioral-equivalence certificate.

## Production Evidence

Both production CLI targets built. ABC-only export
`build/pasteboard-source-block-order-20260925` lets existing conditional-value
regions consume the formerly interleaved inner diamonds of five Builder
callbacks. Each of PageA, PageB, PasteBoard, ProgressBar and ShareDitect loses
22 lines; Home and the common modules are unchanged. Totals are 3,737 lines /
85,088 bytes, 110 lines / 1,020 bytes below result-copies. The outer multi-block
conditional still uses labels; fully nested region recovery remains open.

PageA callback 7089 PCs 32/35 map to the condition prefix [1031,1042), PC 37
to its body operand [1045,1059), PC 41 to its alternate operand [1062,1076),
and the arm jump PC 39 to a zero-width anchor. Partial maps retain 195 methods /
9,745 spans. Statuses remain eight incomplete records, two host constructors
refused at PC 14 and nine metadata-only records. Read-only review found no
concrete defect. No tests, JS execution or RSS measurement ran; full source
equivalence is not established by the new layout or export counts.
