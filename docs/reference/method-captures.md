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

Only the four modern DEFINEFUNC/DEFINEMETHOD encodings are collected. Class,
literal-hidden and historical creation semantics are not inferred from their
absence. Captures used only by deeper closures require later transitive demand
propagation. `kCaptureMissing` means absent direct-use proof, not malformed ABC.
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
complete-pipeline evidence. The existing corpus creation probe does not yet
construct these owned results. Next steps are bounded task/cache admission and
transitive capture requirements, followed by value/Hole/TDZ and source integration.
