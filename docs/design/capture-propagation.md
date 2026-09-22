# Bounded Capture Demand Propagation

This implements the approved typed, lazy capture-analysis direction. It is not
the asynchronous method service or complete closure/source recovery.

## Inputs and Results

The caller supplies a finite forest of explicitly distinguished creation
contexts. Each node borrows an immutable MethodCaptures snapshot and optionally
names a preceding parent context and creation PC. Parent-before-child ordering
gives a termination measure. Multiple contexts may reference the same method;
their identities must not be merged. Building this forest, handling unknown
creation choices and summarizing recursive expansions remain caller work.

Seeds name direct captured requirements in selected contexts. Only demanded
creation edges are semantically checked. Every admitted context/depth/slot fact
is interned once. A transfer either reaches a caller-local allocation site,
references an interned parent demand, or remains at a root capture boundary.
The output is a shared fact DAG, not one copied path per variable. Local IDs
are qualified by context, not promoted to unique runtime cells. Distinct loop
executions still require separate instance reasoning.

## Algorithm and Admission

Use a standard-library ordered map for worst-case logarithmic deduplication,
and a vector of admitted facts as a FIFO worklist. No custom hashing, repeated
fixed-point scans or recursive traversal. Each fact crosses at most one context
edge. Parent context IDs strictly decrease along dependency edges, even when
method IDs repeat. A bounded PMR resource charges map nodes, vector capacities
and vector-growth overlap before allocation. Exact retained fact/index arrays
are admitted while scratch is live. No partial result is published on failure.

For N contexts, Q seeds and F admitted facts, cost is
O(N + Q(log F + log U) + F(log F + log C + log D)); U is direct summary size,
C creation count and D the 32-bit depth domain. Storage is O(N + F), excluding
caller-owned contexts/snapshots/seeds. F can be dense relative to input; budgets
must reject exhaustion explicitly, not silently truncate or claim completion.
Payload admission is not whole-process RSS proof.

The result owns a compact origin/parent registry for all N contexts, so its IDs
remain interpretable after the input forest and snapshots are released. Map
nodes and worklist storage never escape the stack-local PMR resource; published
arrays have independent ownership. Fact references may point forward numerically.

## Implementation Sequence

1. Add a separate capture_propagation unit target with decoded three-generation
   closure fixtures, repeated seeds, distinct contexts and root boundaries.
   Verify the missing API fails before adding production code.
2. Add typed context/fact/result interfaces in include/abcd/analysis and the
   bounded worklist implementation in src/analysis. Preserve MethodCaptures
   direct-use and Rebase contracts; retain no borrowed pointers in output.
3. Test invalid parents/contexts/seeds, demanded target mismatch, local slot and
   sendable failures, exact/short budgets, moves, and 200/201/geometric growth.
   A long repeated-method context chain with repeated seeds must process each
   distinct fact once, not walk the chain independently for every seed.
4. Run Debug CTest, ASan/UBSan, independent review and timed fixture measurement.
   Record limits and commit the verified increment. Corpus construction evidence
   alone must not be relabeled as corpus propagation evidence.

## Verification and Remaining Work

Implemented in `capture_propagation.h/.cc`, with a separate unit target. The
first test build failed on the missing API. On 2026-09-22 the complete Debug
build and 61 CTest cases passed; the propagation test also passed ASan/UBSan.
Independent read-only review found no implementation issue and identified the
storage-complexity correction above.

The decoded fixture covers a leaf-only capture through an empty intermediate
summary, local discharge, outward boundary requirements, converging aliases,
duplicate seeds, separate roots, repeated methods in distinct contexts, missing
seed/context/target/site, invalid parent ordering, slot bounds, sendable refusal,
exact/short budgets, zero-budget empty input, moves and independent output
lifetime. Undemanded invalid semantic edges are explicitly not certified.

Chains of 200/201/4096/16384 contexts each receive as many duplicate leaf seeds
as there are contexts. Each produces exactly N facts and N-1 transfers; the
test checks every indexed fact/parent edge and exact retained payload. This
guards against a per-seed ancestor walk. Budget tests include scratch/final
overlap; a budget covering only retained payload correctly refuses construction.

`/usr/bin/time -v build/decode/capture_propagation_test` exited zero in 0.15
seconds with peak RSS 11,829,248 bytes. This includes synthetic inputs and is
not a full-corpus or complete-source-pipeline memory measurement. The complete
build was running concurrently, so timing is not an isolated benchmark.

The solver is synchronous and demand-driven within a supplied forest. It does
not yet supply the bounded async task/cache service, construct a complete forest
from application metadata, summarize recursive context expansion, recover
runtime values/TDZ, or emit record source. Root boundaries remain unresolved
capture requirements, not globals inferred from absence of a parent. Those
remaining stages must consume these facts without merging runtime instances or
treating successful partial propagation as complete decompilation.
