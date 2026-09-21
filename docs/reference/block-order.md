# Acyclic Block Order

`OrderAcyclicBlocks` supplies a predecessor-before-successor analysis order
for entry-reachable normal flow. It is preparation for forward data-flow
and branch/merge recovery, not a source emitter or instruction scheduler.
It does not narrow the decompiler's requirement to support loops.

The implementation uses Kahn's indegree/FIFO method after iterative entry
discovery. Discovery counts only reachable incoming edges. The result vector
serves as the ready queue; each block is appended once, and parallel edges
are counted and removed separately. FIFO insertion and the supplied successor
order make output deterministic, without a priority queue or repeated scans.

With B input blocks and E input edges, validation scans E once, discovery
scans reachable edges once, and ordering scans emitted blocks' edges once.
Successful acyclic runs visit at most 3E edges and take O(B+E) time, O(B)
additional storage. `OrderWork` records actual edge visits and simultaneous
vector-capacity bytes. All vectors are sized once under `OrderBudget`;
allocation failures return `kResourceLimit`. Input graph storage and allocator
metadata remain outside this pass budget and inside the whole-process budget.

The algorithm family is described by A. B. Kahn, *Topological sorting of large
networks*, CACM 5(11), 1962, DOI 10.1145/368996.369025. This implementation is
original code, not copied dependency source. The publisher endpoint returned
HTTP 403 during this implementation; correctness evidence here is the stated
queue invariant and independent tests, not a claimed full-text paper review.

## Semantic Boundaries

Every input edge is validated, including unreachable ones. Resume edges are
explicitly rejected because they cross activation boundaries. A reachable
cycle returns `kCycle` rather than returning an incomplete order. Unreachable
normal cycles do not invalidate the reachable DAG, and their block count is
reported. Empty graphs, invalid entries and malformed edges are typed errors.

Exception dispatch edges are not present in `NormalFlow`. A consumer must not
use this order to erase catch blocks, infer whole-program reachability, or
move observable effects between branches. It must first account for exception
and suspension semantics, or reject such methods from the acyclic source path.
Loop-capable structuring remains unfinished; this pass is not a substitute.

Tests compare all three-node graphs representable by the normal-flow successor
limit against a separate transitive-closure oracle. Other tests cover shuffled
block IDs, diamonds, duplicate edges, unreachable cycles, invalid unreachable
edges, resume rejection, exact/short budgets, and 1/200/201/4096/65536-block
chains with explicit linear edge-visit checks. The cubic oracle is restricted
to three-node tests and is absent from production.
