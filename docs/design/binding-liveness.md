# Binding Liveness for Phi Pruning

Extend the owned `IdfIndex` with a compact predecessor index and a backward
live-in query. This is the classical sparse worklist solution of
`LiveIn[b] = UpwardUse[b] || (!Def[b] && any LiveIn[successor])`, restricted to
normal-entry-reachable blocks. The existing graph snapshot excludes resumes;
missing exception edges are still not invented.

The reference ordering rule is also used by LLVM 19.1.7
[ComputeLiveInBlocks](https://github.com/llvm/llvm-project/blob/llvmorg-19.1.7/llvm/lib/Transforms/Utils/PromoteMemoryToRegister.cpp):
reads before a block's first store seed backward propagation, while definitions
stop incoming propagation. Here the caller supplies those summaries; no LLVM
code or dependency is imported by this independently implemented query.

The caller supplies defining blocks and blocks with a read BEFORE any local
definition for one binding. A block present in both sets must be live on entry:
its early read cannot be killed by a later write. An ordinary read after the
first definition is not an upward use. Deduplicate IDs; ignore unreachable
blocks, reject invalid IDs. Seed all upward-use blocks, then visit each live
block once, admitting a predecessor only when it does not define the binding.
This handles loops without repeated whole-graph fixed-point rounds.

Build predecessors once with counts/prefix offsets and a flat array, owned by
the same snapshot used for phi placement. Query storage is sparse: ordered
marks for supplied/touched IDs and a FIFO of live blocks, then a sorted owned
result. Cost is O((D+U+L+E_L) log(2+D+U+L)); L/E_L are live blocks and their
incoming edges, D/U supplied input IDs. No graph-sized query initialization,
bit matrix, recursive traversal or per-variable graph clone is allowed.
Several variables can have large overlapping live regions. This is not a claim
of linear whole-method SSA; explicit results themselves may be large. Reuse
the method-wide quota and do not hide repeated work behind fresh per-query limits.

Use the existing mandatory `IdfQuota` across ALL live-in and placement queries
of a method. Each supplied ID, dequeued live block, traversed incoming edge and
output element consumes work. Errors retain consumed work and publish no partial
result. Memory admission covers PMR growth overlap and the final result while
scratch is alive. Index admission now includes predecessor storage. Index and
other retained results remain caller memory, under the whole-process 1 GB cap.

This calculates block liveness, not instruction def/use or memory aliasing.
Lexical cells mutated through closures/calls need sound memory effects before
they can use this query. It does not infer that an unknown lexical load is a
language value, lower Hole/TDZ, rename SSA values or publish source records.

## Implementation Sequence

1. Add a failing `binding_liveness_test` for a diamond with and without an
   upward use at a defining join. Feed its resulting blocks to `Place` with
   the same quota, verifying the phi survives only for the old-value read.
2. Extend snapshot storage and budget admission in `phi_placement.cc`; expose
   owned `BindingLiveness` and `LiveIn` in `phi_placement.h`.
3. Extract the existing private PMR scratch accounting into `idf_scratch.h` so
   the two query implementations use the same allocation/error rules. Implement
   the backward query in `binding_liveness.cc`.
4. Compare all degree-bounded three-node graphs, entries, defining/upward-use
   subsets with a small fixed-point oracle. Test loops, unreachable predecessors,
   duplicates, moves, input lifetime and invalid IDs. Exercise 200/201/4096/65536
   chains, exact/short memory/work quotas and sparse killed-use batches.
5. Run Debug CTest, focused ASan/UBSan and synthetic peak RSS under 1 GB;
   obtain independent review and commit. No source coverage increase is implied.
