# Terminal Arm Ordering

The source-region builder now schedules a branch's single-entry terminal
fallthrough arm immediately after its header. Existing Kahn FIFO ordering can
place the next decision before this arm, preventing the adjacent RegionIf
reduction even for ordinary early-return chains.

This is a local presentation schedule after the unchanged acyclic block-order
certificate. It does not replace the shared ordering algorithm, alter CFG/SSA,
duplicate effects or infer a switch from strings. A moved arm must have one
incoming edge, be the header's fallthrough successor, have no successors, and
terminate with return or throw. Its only predecessor is already emitted and
it has no outgoing constraints, so moving it immediately after that header
preserves topological order. The builder still checks all edges against the
new inverse order before constructing regions.

The forward scan emits each block once, skipping terminal arms already pulled
forward. It costs O(B+E), with one admitted BlockId buffer per block. The
already admitted order-position vector serves as zeroed visitation marks and
is then rebuilt as the final inverse order. No repeated splice or suffix scan
is used. Loop and exception-scope region builders are unchanged. Existing
RegionIf admission still proves its arm/join structure; staged argument
handling and effect/SSA validation remain independent.

## ABC-Only Evidence

Both production CLIs built successfully and read-only review found no concrete
defect. Export `build/pasteboard-terminal-arms-01` renders the three comparisons
in CommonConstants.GetValue as direct if/return arms rather than breaks to
later return blocks. Three conservative surrounding labels still remain;
removing them requires accounting for actual emitted jump references rather
than deleting labels based on text. Eight incomplete application records and
two host-constructor refusals at PC14 remain unchanged.

Tests, generated JS execution and RSS remain paused. Deferred regressions
include early return/throw chains, shared terminal blocks, duplicate edges,
independent branches, nonterminal arms, loops/exception exclusions, staged
arguments, reordered PC spans, resource limits and 200/201 scaling. This is
source-generation evidence, not runtime-equivalence or complete structuring.
