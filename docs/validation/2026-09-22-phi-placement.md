# Phi Placement Validation

This increment adds the owned IDF index and budgeted placement queries described
in `../design/phi-placement.md`. It is a prerequisite for lexical/register SSA,
not lexical source emission. No source coverage count changes are claimed.

## Correctness

`phi_placement_test` compares against the mathematical dominance-frontier
definition, not the optimized production traversal. All 343 directed
three-node graphs satisfying the two-successor bound are checked for every
entry, defining subset, and live-in subset, plus unpruned mode: 74,088 queries.
Another 500 seeded sixteen-node graphs test both pruned and unpruned placement,
bringing oracle comparisons to 75,088. These include cycles, disconnected
nodes, duplicate edges, irreducible flow and nonzero entry IDs.

The large fixtures use 200, 201, 4,096 and 65,536 blocks. A straight chain has
no phi candidates: each entry query inspects one range node and no CFG edges.
Repeated leaf queries do not clear a graph-sized array. Reverse-order chains
with a self-edge at every block produce every block as a phi site, test sorted
output and alternating live-in pruning, and inspect each candidate source once.
Deep trees and range queries use bounded explicit stacks, not input recursion.

The adversarial family with a backedge from every block to entry demonstrates
the retained limitation: one output phi can require B candidate sources and
2B-1 edge visits. A shared batch quota admits one query and stops the second
with `kWorkLimit`. This is a work-admission bound, NOT evidence that repeated
candidate sets have been asymptotically eliminated. Method-level integration
must reuse one quota across bindings and may require additional reuse before
previously rejected workloads can be admitted.

Tests also cover duplicates, moved-from index/result safety, source graph
lifetime, malformed IDs/edge kinds, resume refusal, exact/short memory budgets,
exact/short/zero work quotas and consumed work surviving memory failure.
Independent review identified the moved-from and invalid-edge issues; both
were fixed and covered before commit. Review also checked the monotone-depth
argument for subtracting already processed dominance intervals and quota paths.

## Execution

```sh
cmake --build build/decode -j2
ctest --test-dir build/decode --output-on-failure
cmake --build build/decode-asan --target phi_placement_test -j2
ctest --test-dir build/decode-asan -R '^phi_placement_test$' --output-on-failure
prlimit --as=1000000000 -- /usr/bin/time -v build/decode/phi_placement_test
```

Debug CTest passed 65/65; the focused ASan/UBSan test passed. The standalone
Debug synthetic run exited zero in 1.92 seconds, peak RSS 20,295,680 bytes,
under the inherited 1,000,000,000-byte address-space limit. Builds/tests had
finished before this measured run. The tested executable does not spawn workers.
This measures only the synthetic analysis test, not the full decompiler, Node,
or a real-corpus SSA pass. Index/query payload accounting also excludes allocator
overhead and fixed call frames; the whole-process requirement still needs RSS
validation when integrated.

No full-corpus run was repeated for this analysis-only addition: the source
probe does not call the new API. Liveness, SSA renaming, Hole/TDZ value flow,
closure mutation, source bindings and record-level output remain unimplemented
integration work. Normal-only phi placement is not valid over omitted exception
or resumption edges.
