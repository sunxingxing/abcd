# Captured Binding Substitution

`RebaseCapture` implements one edge of the approved closure-binding design:
translate a callee captured-depth/slot pair through one ordinary creation
context into the caller's environment index. It does not assign each method
one global lexical parent or emit source closures.

## Semantics

`CapturedSlot` accepts only an entry-chain depth and a lexical slot. Callee-local
environment IDs are not accepted as captured bindings. For creation environment
E, depth d and slot s, the result is `(Ancestor(E, d), s)` in the caller context.
Depth zero selects E itself. If the result is a caller-local environment, its
declared slot bounds are checked; if it is another captured chain, bounds remain
unresolved and the caller may substitute it through the next creation edge.

This follows the pinned ordinary creation semantics documented in
[creation-binding.md](creation-binding.md): the callee entry environment is the
one stored by its creation. The prior environment analysis already accounts for
callee-local pushes and pops before returning a captured binding. Do not add
those depths again or add an extra level for the function call.

Sendable capture is explicitly rejected by this ordinary-chain operation. Its
empty shared-function environment and separate module environment cannot be
reinterpreted as the caller's ordinary lexical chain. Slot identity does not
prove value initialization, Hole/TDZ behavior or language-level source syntax.

IDs in the result belong to the caller's index. They must remain paired with
that caller and creation context; equal method targets or allocation-site IDs
do not identify a shared runtime cell. Multiple creation sites, recursive
creation and fresh loop instances must stay distinct. This operation neither
recurses through a closure graph nor chooses one of several possible parents.

## Algorithm and Memory

The immutable jump-pointer index now covers the full uint32 captured-depth
domain, with 32 levels instead of 16. ISA `LexicalDepth` stays uint16; it must
not be used to store composed analysis offsets. The existing narrow `Resolve`
and `Ancestor` interfaces remain available. `ResolveCapture` uses the same
ancestry logic without narrowing and checks local slot bounds afterwards.

A fixed 32-level table was chosen over repeated maximum-width jumps, which
would introduce depth-dependent work at every use, and over a new optimal
level-ancestor implementation, whose complexity is not justified for this
bounded domain. The tradeoff is a larger per-environment payload. Allocation
admission and reported payload use `sizeof(Node)`, so the larger table is not
hidden from the budget. No new dependency is introduced; provenance remains
documented in [environment-index.md](environment-index.md).

Construction/storage are O(N log D), and one substitution is O(log D),
D=2^32, with no query allocation or recursion. Captured-chain addition checks
overflow before adding. Slot values are not truncated. The whole-process
1,000,000,000-byte limit still applies to caller data and all live analyses.

## Verification and Integration

Tests were introduced before the API existed and failed on missing symbols.
They cover different creation contexts, caller-local bounds, unresolved outer
chains, multi-edge composition, invalid local IDs, sendable refusal, uint32
overflow and the exact maximum captured depth. A small independent parent-walk
oracle checks seeded forests; chains at 200/201/4096/65538 nodes exercise
deep queries, including a 65536-level local jump that cannot fit in ISA depth.

`creation_binding_test` additionally decodes a callee lexical read, obtains its
captured binding from CFG analysis and substitutes it through two actual bound
creation sites, producing local and external results respectively.

This is a query primitive, not an all-program materialized capture graph. The
next integration must reuse per-method capture summaries and creation edges
without decoding a target again for every site or expanding the cross-product
of all sites and uses. Requests should remain lazy and budgeted, with explicit
context lifetime and cycle handling. No record-source recovery follows solely
from successful substitution.

## Measured Verification

On 2026-09-22, full Debug CTest passed 58/58. ASan/UBSan passed the four
capture-rebase, creation-binding, environment-index and environment-flow tests.
The Debug capture-rebase test completed in 0.13 seconds with peak RSS
26,779,648 bytes, including its synthetic metadata and indexes. An independent
read-only review found no correctness issue; a suggested wide overflow test
across nonzero captured anchors was added and passed.

The enlarged index was also exercised by the complete creation corpus probe:

```sh
cmake --build build/decode-release --target abc_creations -j2
/usr/bin/time -v build/decode-release/abc_creations tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code \
  > build/creation-bindings-wide.json
jq 'del(.results)' build/creation-bindings-wide.json > tests/corpus/creation-bindings-wide.json
jq -e --slurpfile previous tests/corpus/creation-bindings.json \
  '(del(.results, .peak_rss_bytes) == ($previous[0] | del(.peak_rss_bytes))) and (.peak_rss_bytes < .address_space_limit_bytes)' \
  build/creation-bindings-wide.json
```

Exit zero, wall time 83.15 seconds, peak RSS 163,188,736 bytes, enforced address
space ceiling 1,000,000,000 bytes, no child workers. All summary fields except
RSS exactly matched the previous creation probe, including 2,780,261 bound
sites and zero target failures. The synthetic query suite ran concurrently
with part of this probe; elapsed time is not an isolated comparative benchmark.
The full probe validates enlarged-index regression behavior and memory, not
all-corpus capture substitution: that new query is currently exercised by unit
and decoded-method integration fixtures, not a complete cross-function driver.
