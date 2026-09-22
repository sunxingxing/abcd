# Sparse Phi Placement

This implements the SSA placement choice in `algorithms.md`, a prerequisite
for tracking lexical Hole/environment values without leaking them into JS.
It does not yet rename values, infer liveness, lower TDZ, or emit lexical source.

## Contract

Build an owned `IdfIndex` once from a normal CFG and its chosen entry, computing
dominators internally so a mismatched tree cannot silently be used. Reject
resume edges; exceptional edges are not present and must not be inferred.
The snapshot owns compact successors, dominator subtree intervals, depths,
inverse preorder and a segment tree of minimum successor depths.
It may outlive the input graph and serve concurrent immutable queries.

Each `Place` query takes defining blocks and optional live-in blocks for ONE
binding. Missing liveness means unpruned IDF; an explicitly empty set means no
live phi sites. Duplicate input IDs are harmless, out-of-range IDs fail, and
unreachable IDs are ignored. Results are unique ascending block IDs. Correct
instruction-level def/use and liveness remain caller obligations, especially
uses before a local definition and values crossing calls/capture boundaries.

Use deepest-first iterated dominance frontier traversal, following Sreedhar-Gao
and the implementation strategy in LLVM llvmorg-19.1.7
`llvm/include/llvm/Support/GenericIteratedDominanceFrontier.h`:
https://github.com/llvm/llvm-project/blob/llvmorg-19.1.7/llvm/include/llvm/Support/GenericIteratedDominanceFrontier.h
Reference source SHA-256:
`162779efbe092ee22086a9088072f26df23d4402c0b4861b8b5a5cdb69d2bb51`.
Retained license SHA-256 (only the trailing blank line removed):
`3340babe8ac7bc6ae294d93aa01c310a250d43d5b760e5c12954882d4e5c83c7`.
Retain its Apache-2.0 WITH LLVM-exception notice for the adapted traversal.
No LLVM runtime dependency is introduced. Do not precompute a dense frontier
matrix or walk the entire graph afresh for every variable.

Instead of LLVM's direct subtree walk, report only sources in the root's
preorder interval having a successor at depth no greater than that root's.
The segment tree prunes ranges whose minimum is deeper. Process roots in
descending depth, with preorder as deterministic tie-breaker. Already processed
root subtrees are recorded in an ordered disjoint-interval map. Subtract them
before reporting, then merge into the new root interval. Dominator subtrees
are laminar; a previously processed contained subtree used an equal or greater
depth threshold, so every edge relevant now was already considered. Its dead
frontier candidates remain dead because query liveness is fixed. Each interval
is inserted and erased at most once; each candidate source is inspected once.
This preserves IDF semantics without repeated walks through irrelevant tree
paths. Tests must compare this refinement to the mathematical DF definition,
not merely the LLVM traversal.

## Cost and Ownership

One-time index construction uses existing O((B+E) log B) dominators and an O(B+E)
snapshot pass. Per-query ordered sparse marks, depth heap, interval map and
range reporting give O((D+L+M+F) log(2+B+D+L)) time: D/L count supplied
definitions/live-in IDs, M candidate CFG source blocks, F returned sites.
Each source has at most two successors. There are at most D+F roots and an
amortized O(D+F) interval gaps; each gap costs O((1+matches) log B) to report.
There is no B-sized query initialization or reset. Query heap storage is
O(D+L+M+F), plus fixed address-depth traversal scratch. No untrusted hash
collision assumption is used. Multiple bindings can still have genuinely
large candidate edge sets: this is NOT a globally linear SSA claim. Work
counters expose range, block, edge and heap visits to later admission.

In particular, a chain with every node also branching to entry has only one
phi but M=B candidate sources per query. Repeating N different defining sets
can still cost O(NB); this is not acceptable as an unchecked multi-variable
pipeline. Every `Place` therefore requires an explicit noncopyable `IdfQuota`.
The future method-level SSA pass must reuse that quota across ALL bindings,
never renew it for each variable. Charge supplied IDs, heap pops, range nodes,
candidate edges, interval merges and final output items. These are logical
units, each with at most amortized logarithmic container cost, not CPU cycles. Failed
queries retain their charges; exhaustion is `kWorkLimit`, never partial phi
output or silent loss of semantics. Independent workers use separately admitted
quotas. This bounds work rather than solving the general repeated-target
problem; further candidate-set reuse remains necessary where admission would
otherwise reject real methods. No multi-variable SSA scalability claim is made.

Each query has a byte budget with actual PMR allocation accounting, including
vector growth overlap and final owned output while scratch remains live. Index
construction admits its fixed arrays and simultaneous dominator storage.
Fixed call-stack scratch, allocator overhead, input graph and independently retained results still belong
to the whole-process 1 GB budget. Errors publish no partial placement result.
The synchronous algorithm is not the asynchronous function task service.

## Implementation and Verification

1. Add `tests/unit/analysis/phi_placement_test.cc` and its CTest target, observe
   failure before adding the missing API. Check diamond, self-loop, iterated
   frontier and pruned-empty behavior.
2. Add `include/abcd/analysis/phi_placement.h`, index construction in
   `src/analysis/phi_placement.cc`, and the bounded sparse query implementation.
3. Compare all degree-bounded three-node graphs against a deliberately slow
   mathematical dominance-frontier oracle, with defining/live subsets; exercise
   larger random graphs, unreachable blocks, duplicate edges and arbitrary entry.
4. Test 200/201/4096/65536-node families, deep trees without recursion, repeated
   leaf queries, moves/input lifetime, byte limits and malformed graphs.
5. Run Debug CTest and focused ASan/UBSan, obtain independent review, then commit
   the verified increment. Do not claim increased source coverage from this
   analysis-only change.
