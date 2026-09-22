# Binding Liveness Validation

The new `IdfIndex::LiveIn` query computes normal-reachable live-in blocks for
caller-supplied defining blocks and upward uses. It shares the owned graph,
allocation accounting and mandatory work quota with phi placement. See
`../design/binding-liveness.md` for the semantic and complexity contract.

## Evidence

The test independently computes graph reachability and a dense mathematical
live-in fixed point on small graphs. All 343 degree-bounded three-node directed
graphs are checked for all entries and every definition/upward-use subset:
65,856 comparisons. Another 1,000 seeded sixteen-node graphs bring the total to
66,856. Exact block and incoming-edge visit counts are checked, not only results.

The diamond fixture passes computed liveness into `Place`. A join that reads
before its local definition retains its phi; removing that upward read yields
no live-in and no phi. An exact combined quota covers both queries, and an
exhausted liveness quota prevents a subsequent placement query from resetting
its allowance.

Fixtures of 200, 201, 4,096 and 65,536 blocks cover full live chains, loops,
repeated killed-use queries, and high fan-in with duplicate edges. A killed-use
query visits one live block and one predecessor edge regardless of graph size;
high fan-in is correctly charged by actual incoming-edge count. The large
fixtures never use the quadratic test oracle. Exact/short memory limits include
simultaneous result and scratch storage; work-limit and memory failures retain
consumed quota. Invalid IDs, duplicates, unreachable predecessors, moved-from
objects, nonempty move assignment and results surviving index destruction are
also covered.

Independent read-only review found no required correction. The existing phi
placement suite was rerun after extending snapshot storage and extracting the
shared PMR scratch resource.

## Execution

```sh
cmake --build build/decode -j2
ctest --test-dir build/decode --output-on-failure
cmake --build build/decode-asan --target binding_liveness_test phi_placement_test -j2
ctest --test-dir build/decode-asan \
  -R '^(binding_liveness|phi_placement)_test$' --output-on-failure
prlimit --as=1000000000 -- /usr/bin/time -v build/decode/binding_liveness_test
```

Debug passed 66/66 tests. Both focused tests passed ASan/UBSan. After builds and
tests finished, the standalone Debug liveness test exited zero in 0.93 seconds,
peak RSS 22,073,344 bytes, under an inherited 1,000,000,000-byte address-space
limit. It spawned no workers. This is a synthetic analysis measurement, not a
whole-decompiler or real-corpus SSA memory claim.

No real-corpus source probe was repeated: the source pipeline does not yet use
this analysis. Instruction-level def/use extraction, alias-safe lexical cell
effects, SSA renaming, sentinel/TDZ lowering, wrappers and record publication
remain required. No additional function body or application record is counted
as decompiled by this increment.
