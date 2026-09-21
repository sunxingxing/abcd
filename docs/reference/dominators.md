# Normal-Flow Dominators

`BuildNormalDominators` implements the simple Lengauer-Tarjan algorithm with
LINK(parent, child), minimum-semidominator EVAL, deferred buckets and the final
immediate-dominator correction pass. It is an independent implementation of the
algorithm specified in the project's algorithms design, not copied third-party
code. Reference: Lengauer and Tarjan, *A Fast Algorithm for Finding Dominators
in a Flowgraph*, ACM TOPLAS 1(1), 1979, DOI 10.1145/357062.357071. No additional
library dependency is introduced. The design links a copy of the paper; attempts
to retrieve that copy during implementation timed out.

All traversals are iterative, including DFS, path compression and tree indexing.
Predecessors use counted contiguous slices. Buckets and tree children use index
links, not per-node heap objects. Reachable vertices receive DFS numbers; incoming
edges from unreachable vertices are excluded from semidominator evaluation.
Duplicate edges and self loops are valid. All encoded targets, including those
in unreachable blocks, are validated before analysis.

Complexity is O((B+E) log B) for simple linking with path compression, with O(B+E)
storage. No dominance sets or per-variable graph scans are constructed. The result
contains one node per input block, an optional immediate parent, and half-open
tree traversal intervals for constant-time dominance queries. Root and unreachable
nodes have absent parents; zero interval begin identifies unreachable nodes.

The budget covers vector capacities for the result and all simultaneously live
scratch, checked before each one-time allocation and verified after allocation.
There are no growing buffers or recursive stacks. Counters expose edge visits,
compression steps, and summed allocated capacities. Allocator bookkeeping and
fixed stack objects still require whole-process RSS verification; capacities alone
are not a proof of the user's 1 GB process limit.

Scope is deliberately explicit: the input is NormalFlow, including its resume
edges, without exception dispatch edges. This result must not be used as SSA
dominance across exceptions or interpreted as proof that resumption is immediate.
Handler regions reachable only by exceptional transfer remain unreachable here.

Tests compare all permitted three-node graphs at every entry and 1,000 seeded
16-node graphs against a vertex-removal reachability oracle. This slow oracle is
test-only. Deep chain/backedge graphs cover 200, 201, 4096 and 65536 nodes, with
work-counter limits and exact-budget/one-byte-short cases. Invalid entries,
targets and successor counts are rejected. The corpus CLI validates construction
and budgets, not an independent oracle for every real graph and not source output.
