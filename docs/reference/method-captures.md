# Owned Method Capture Facts

`MethodCaptures` combines the existing environment index, compact capture
summary and reachable modern function-creation bindings. It is an immutable,
move-only method result, not a cache or asynchronous executor. No application
record source is produced by this stage.

## Ownership and Context

Build requires matching, unchanged file/header/metadata/lookup/body/flow inputs
and a genuine once-per-file fingerprint. `CaptureOrigin` contains that SHA-256,
profile and method position. It is context provenance, not a complete cache key:
future task keys also require stage, engine/rule revisions and options.

Only after all bindings succeed does Build transfer `flow.environments`.
Failure leaves the input ancestry unmoved. After success, the remaining flow
must not be reused for analysis because its local IDs depend on that ancestry.
The result owns its ancestry, summary and exact-size creation array; it retains
no input reader, body, graph or metadata pointers. Const queries can be shared
while result lifetimes are stable, without concurrent moves or destruction.

Each creation site remains distinct. Environment IDs describe allocation sites,
not unique runtime cells: loops and repeated calls still create fresh instances.

## Query Contract

Resolve checks file/profile, binary-searches the creation PC, verifies the
target method position and checks membership in the target's direct captured-use
summary before calling `RebaseCapture`. The result is relative to the caller.
The query performs no allocation, file access, target decoding or recursive
graph traversal. Sendable environments remain a typed refusal.

`Rebase(creation, target_origin, demand)` transfers a target-entry-relative
requirement supplied by a demand analysis without requiring a direct target use.
It shares Resolve's context/creation/target checks, then applies the same
ancestry/known-slot validation. The caller owns provenance and reachability
evidence; success does not prove that a use exists or a value is initialized.
Its cost is O(log C + log D), with constant auxiliary space and no allocation.

This distinction matters when a leaf reads an outer variable but its immediate
parent only creates the leaf. Resolve can establish the leaf's direct use and
return a captured demand in the parent's context. Rebase then maps that demand
through the parent's creation even though the parent's direct summary is empty.
A local result is discharged at a caller allocation site; a captured result
remains an unresolved outward demand. Preserve the creation context with either
result, rather than interpreting method position or site ID as runtime identity.

Only the four modern DEFINEFUNC/DEFINEMETHOD encodings are collected. Class,
literal-hidden and historical creation semantics are not inferred from their
absence. Captures used only by deeper closures require transitive demand
propagation; Rebase provides its single-edge transfer, not its graph solver.
`kCaptureMissing` means absent direct-use proof, not malformed ABC.
This API alone does not recover a complete closure graph or lexical source names.

## Budget and Complexity

The default payload budget is 64 MiB. Admission includes transferred ancestry,
summary construction scratch and retained summary/sites at their actual overlap.
Summary scratch is released before creation-array allocation. Inputs, allocator
overhead and object/stack storage need separate whole-process admission. Typed
resource failures preserve semantics; payload counters do not prove the 1 GB
whole-process requirement.

For I instructions, L captured accesses, C creation sites, R pool regions and M
indexed methods, construction is O(I + L log L + C(log R + log M)), excluding
the already-completed environment analysis. Query is O(log C + log U + log D),
where U is the target's distinct direct requirements and D is the 32-bit depth
domain. No site/requirement cross-product is materialized. Algorithms are the
existing sorted summaries, indexed target resolution and binary-lifting ancestry;
this composition adds no external dependency.

## Verification

On 2026-09-22 the fresh Debug build and all 59 CTest tests passed. The extended
`creation_binding_test` passed under ASan/UBSan. It checks distinct local/external
creation contexts, missing sites/targets/requirements, context mismatch, sendable
refusal, failure-preserved ancestry, moved-from queries and queries after input
file/body/metadata release. Exact and one-byte-short budgets exercise both
site-array and summary-scratch dominated peaks. Sizes 200/201/4096 check retained
site payload and every indexed query. A synthetic alternate identity checks the
context guard; fingerprint authenticity remains the caller's contract.

`/usr/bin/time -v build/decode/creation_binding_test` completed in 0.03 seconds
with peak RSS 9,129,984 bytes. This is a fixture measurement, not full-corpus or
complete-pipeline evidence.

## Corpus Integration

`abc_creations` now constructs one owned result after its independent summary
and creation-site probes. It reuses the validated once-per-file SHA-256, never
loads target bodies, and releases the result before advancing to the next
method. This deliberately repeats a bounded number of analysis passes to retain
independent baseline counters; it does not add a per-target body scan.

`method_capture_results + method_capture_failures = creation_methods` counts
every environment-accepted method. Environment-rejected methods do not reach
construction. `method_capture_obstacles` counts one typed failure per refused
construction, separately from the existing mixed-unit creation histogram.
`method_capture_peak_bytes` is the maximum successful construction payload, not
process RSS or a bound on unsuccessful allocation attempts. Exit zero continues
to mean inspection completed, not that every method can be decompiled.

Next steps are bounded task/cache admission and transitive capture requirements,
followed by value/Hole/TDZ and source integration. Corpus construction does not
yet exercise inter-method Resolve queries; those remain fixture-tested.

See the [corpus validation report](../validation/2026-09-22-method-captures.md)
for construction counts, limitations and process memory evidence.

## Demand Transfer Verification

`capture_demand_test` decodes a three-method fixture in which only the leaf reads
the captured slot. It checks root-local and outward mappings through an empty
middle summary, pop-induced depth shifts, local discharge, slot bounds, depth
overflow, file/profile/target/site mismatch, sendable refusal and unchanged
Resolve error precedence. Sets of 200/201/4096/65535 demands preserve slot IDs
without growing retained result payload. The test initially failed compilation
because Rebase was absent, then passed with the implementation. Both this test
and creation_binding_test passed under ASan/UBSan. Independent read-only review
found no correctness issues.

The complete Debug build and all 60 CTest cases passed after this change.

On 2026-09-22 the Debug demand test took 0.04 seconds with peak process RSS
8,126,464 bytes. This is synthetic query evidence, not full-corpus transitive
recovery or complete-pipeline memory evidence.

The next solver must use a bounded sparse worklist, deduplicate admitted
context/depth/slot requirements and retain provenance separately. It must charge
pending facts, indexes, provenance and retained snapshots, and report demand-edge
work honestly. Do not walk the entire creation graph separately for every
variable, eagerly construct a site/use cross-product, or assume a small number
of fixed-point rounds. Recursive creation contexts and depth-increasing cycles
need a separate summary/termination contract; deduplication alone is insufficient.
