# Method Capture Requirements

`CaptureSummary` compacts the captured lexical accesses of one successfully
analyzed method into sorted depth/slot requirements with access-site counts.
It is an immutable input for lazy cross-function capture queries, not a global
closure graph or runtime-cell deduplication scheme.

## Contract and Algorithm

Inputs are the unchanged `BoundLexicalSite` bindings from one valid method
environment analysis. Local environment bindings are excluded. Captured depths
retain all 32 bits and lexical slots retain their ISA width. Equal depth/slot
pairs share a summary entry, but different creation contexts never become one
runtime variable. Counts include both reads and writes; use instruction offsets,
access direction, effects and value/TDZ state remain in other analyses.

Build first counts captured sites, admits a fixed sorting scratch array, sorts
typed pairs and counts unique keys. It then admits an exact-size result array
while scratch is still live, fills access counts, and releases scratch. A single
slot used many times retains one entry rather than an array sized to all uses.
`Find` uses binary search. No per-use full graph walk or all-pairs operation is
introduced.

For N input bindings, C captured accesses and U distinct captured pairs:
construction is O(N + C log C), retained payload O(U), and lookup O(log U).
Build payload peak is `C*sizeof(CapturedSlot) + U*sizeof(CaptureUse)`.
Standard sorting adds logarithmic stack space; allocator overhead and caller
flow/body memory are outside the payload allowance but inside the process limit.
The default allowance is 64 MiB; allocation/admission failures are typed.

A hash table was not selected: sorting provides predictable worst-case lookup,
compact fixed arrays and exact payload accounting without per-entry allocation.
No new algorithm dependency or external code is introduced. The existing
whole-process 1,000,000,000-byte ceiling remains binding.

Returned spans/pointers live with the move-only summary. Const queries can be
shared while ownership/lifetime is stable; moving or destroying it concurrently
is not allowed. Method/file/profile identity remains caller-owned. This is not
the bounded asynchronous executor or a cache eviction policy.

## Batch Integration

`abc_creations` now builds one summary for every environment-accepted method,
records its counts and releases it before advancing. No target is decoded
again per creating site and no site/use cross-product is materialized.
`capture_summary_methods + capture_summary_failures = creation_methods`.
`capture_unique_slots` sums per-method distinct requirements, not globally
distinct variables. `capture_access_sites` counts their lexical occurrences.
`capture_summary_peak_bytes` is the maximum successful summary-build payload,
excluding failed builds and the rest of the process.

Summary refusal does not prevent the independent creation-site probe. Its typed
error is added to the obstacle histogram, whose mixed-unit total now equals
environment-rejected methods + failed creation sites + failed summaries.
Exit zero still means inspection completed, not a completed decompilation.

## Verification and Next Integration

The unit test failed before the new header/API existed. It checks filtering,
ordering, duplicates/counts, absent keys, zero/exact/one-byte-short budgets,
empty inputs, moved-from state and move assignment. Sizes 200/201/4096/65536
exercise distinct and repeated requirements, logarithmic lookup and exact
retained payload. The CLI test first failed on missing summary fields, then
checks summary counts from a decoded ABC fixture alongside existing creation
and static-exclusion tests.

[MethodCaptures](method-captures.md) now owns summaries and caller contexts and
queries direct captured uses at creation sites without retaining input bodies.
Bounded cache/task admission and transitive requirements remain unimplemented.
Missing exception/sendable/class coverage and value/TDZ
recovery remain separate obstacles. Neither a compact summary nor a successful
lookup constitutes generated source or a recovered record.

Full-corpus counts and process memory are recorded in the
[validation report](../validation/2026-09-22-capture-summaries.md).
