# Register Value Categories

## Boundary and Domain

Analyze an owned RegisterSsa together with the unchanged validated body from
which it was built. Classify possible normal-completion VM values, not source
types, expression purity or source equivalence. Four typed atoms form a finite
powerset: language value, Hole, internal environment, other internal value.
Bottom is no established value and is NEVER a language-value proof. Top includes
all four atoms. Unspecified method entries are top; caller-supplied entry facts
must name entry-origin SSA values, be nonempty and unique. Their soundness is a
caller precondition, not inferred from a register's spelling or frame position.

Copy instructions preserve the full set. Phi nodes union all normal inputs,
including method-entry operands. Constants are language values; LDHOLE is Hole;
NEWLEXENV writes an internal environment. Current environment and lexical loads
remain top without scope/cell facts. Function creation produces a language
function on normal completion; that fact does not prove wrapper/home/capture
equivalence. Calls and property results remain top without callee/heap proofs.
Selected boolean/string-producing scalar operations have language results when
all operand categories are language-only; otherwise top, or bottom until each
dependency has evidence. Numeric results need stronger numeric facts: accepted
noncanonical NaNs can become impure tagged encodings under negation. FLDAI raw
bits at or above the pinned impure-NaN threshold remain top, not language values.
Unknown inputs never become language values merely because an ISA writes acc.

NaN evidence is pinned with the repository reference lock: `interpreter-inl.cpp`
FLDAI (near 1253) bit-casts the raw immediate, and NEG (near 4892) negates the
decoded double; `js_tagged_value.h` constructor (near 119) adds
DOUBLE_ENCODE_OFFSET after a debug assertion, and IsImpureNaN (near 328) tests
`TAG_INT - DOUBLE_ENCODE_OFFSET`. `js_tagged_value_internals.h` defines the
threshold as 0xfffe000000000000 for this layout. With assertions disabled,
0xffff000000000005 plus the offset wraps to Hole (5). Accepted positive NaN
0x7fff000000000005 becomes that impure pattern under NEG. These are source-level
counterexamples, not a claim that a built Ark runtime was executed in tests.

Hole checks do NOT globally narrow their input SSA value: a use before the
check, another path or the exceptional outcome could still observe Hole.
Use-site/path refinement and exception execution need separate analyses.

## Algorithm and Cost

Use the standard finite-lattice monotone worklist, independently implemented.
Reference: [Clang data-flow introduction](https://clang.llvm.org/docs/DataFlowAnalysisIntro.html),
read 2026-09-22; no third-party implementation is imported. Prefer an SSA
dependency graph to per-register CFG sweeps. Construct outgoing dependencies
as CSR in linear time. Copies/Phis receive changed source facts incrementally,
never rescan a Phi's complete input list on every update. Unary/binary rules
inspect at most two fixed operands. Each value gains at most four atoms, so
each outgoing edge is visited at most four times. No recursive traversal.

Let V be SSA values, D actual dependencies, F supplied facts, I method size.
Rule extraction uses existing indexed SSA read/write/Phi queries and reviewed
typed lifts: O((V+D) log(2+I+V+D)+F). CSR construction and solving are O(V+D+F),
with a fixed four-atom height; output storage O(V), scratch O(V+D). The shared
method quota charges extraction, adjacency construction, propagation and copy.
PMR admits vector growth overlap and final owned output while scratch is live.
The caller separately admits retained input body/SSA bytes and other tasks.

## Implementation Plan

1. Add failing `tests/unit/analysis/register_value_flow_test.cc`: LDHOLE -> STA
   -> LDA must stay Hole, not become a language value. Add its CMake target and
   run the target before implementing the header.
2. Add `value_kinds.h` for the closed typed domain and
   `register_value_flow.h/.cc` for owned results and bounded sparse propagation.
   Keep instruction-rule classification in `register_value_rule.h/.cc` under
   src/analysis so it is not a generic public semantic API.
3. Test constants/copies/merges/loops/unknown calls and loads, exact supplied
   entry facts, non-refinement at Hole guards, internal-environment copies,
   malformed origin/fact boundaries, moves/lifetimes and budget failures.
4. Compare small generated CFGs against an independent slow fixed-point oracle.
   Exercise 200/201 and geometric chains/high-fan-in Phis with work counters;
   run full Debug and relevant ASan/UBSan, request review, measure RSS under a
   1G cap, record evidence and commit. Do not count this as recovered source.
