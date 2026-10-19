# Round 83: mixed iteration ownership evidence

This increment changes diagnostics only. The attempted local repair was
removed after the original-flow ownership conflict was exposed. No tree,
control or recovery admission is widened.

## Direct refusal

DeepTypeUtils (record 38853), m39263 contains both native enumeration and native
iteration protocols. The for-in acquisition is getpropiterator at PC20,
protected by Try1, body18/handler25. Its handler stores the caught payload at
PC25, reloads it at PC27 and throws it at PC29. The later for-of acquisition is
getiterator op60, PC145 (working PC145), result Value49, working block120.

TreeBuilder::Build verifies the for-of plan with its immutable input, then
verifies the for-in plan through VerifyPhasedEvaluationPlan without that input.
VerifyForOfEvaluation receives a nonempty plan and a null input. This is not a
value escape or a phi disagreement. The new fixed-size witness reports
EvaluationObligation::kProtocolContext / ProtocolContextCheck::kMissingInput
at the live iterator acquisition. Missing-plan is a separate diagnostic. No
use identity is fabricated for an input-context mismatch. Existing enum values
and resource classes remain unchanged. Both JSON paths share the encoding.

## Why a one-line repair is insufficient

Temporary probes (all instrumentation and behavior edits removed) passed the
missing input and adapted the for-in view to the already-projected ForwardedTry.
The latter had dereferenced an absent TryControlRegion: there is no physical
wrapper left to bypass. The existing per-entry forwarding proof still applies,
but wrapper absence alone is not proof.

The next independent tree check refused duplicate operation ownership:

| Original operation | PC | Competing ownership |
|---|---:|---|
| op11 | 25 | for-in forwarding handler and for-of kForwarded member |
| op12 | 27 | for-in forwarding handler and for-of kForwarded member |
| op13 | 29 | for-in forwarding handler and for-of kForwarded member |

ForOfEvaluation's Index assigns the protocol seal's transparent forwarding
operations to a for-of member. ForInEmissionView independently records the
same handler for the for-in acquisition. TreeBuilder records both, and
CheckTreeOrigins rejects the duplicate. TreeFlow's placement checks require
these owners to be ForOfStmt and ForInStmt respectively. Removing one builder
record alone would not satisfy both independent obligations. The new real
es2abc mixed-loop fixture reproduces the direct refusal and, in the temporary
probe, the duplicate handler operation (op15).

## Follow-up boundary

A shared forwarding-entry disposition must be established before tree emission:
original exception exit -> payload/destination -> acquiring protocol occurrence.
Separate the global proof that a handler is transparent from the source statement
that owns each routed entry. A shared handler may have several entries; neither
protocol may silently appropriate the other's entry. The evaluation disposition,
emission views, OriginalFlow and recovery must consume and independently
recompute this correspondence. Already-certified transparent forwarding is
reused; effectful close code must remain distinct. This is joint protocol
ownership work, not a new source-language premise, and is outside this round's
conditional local-fix authorization.

The diagnostic mismatch branch scans live raw acquisitions once, bounded by the
work budget, with fixed-size evidence and no new allocation. Successful proof
paths and their budgets are unchanged.

## Acceptance

- Full `cmake --build build/regression -j 6 -- -k`: passed.
- Fixed `ctest --preset source-regression -j 1`: 65/65.
- `^(iteration_context|for_of_|protocol_topology|tree_failure_test|evaluation_failure)`:
  29 tests pass after correcting the new CLI harness to expect abc_semantic's
  refusal exit status (initial run 28/29; corrected test rerun 1/1).
- `cli|corpus|failure|_receipt|manifest`: 78/79. Only the registered pre-existing
  family_source_cli_test fails (`node escaped its owner or origin arena`).
- Current-checkout baseline vs final cohorts: previous flags 526/620 unchanged;
  V4 573/620 unchanged; no status or source text changes. Full exported-directory
  byte comparison: old flags identical; V4 only o32/manifest.json differs, solely
  record38853's located diagnostic. No added/removed output files.
- No new publication or engine-output Node differential is claimed. All
  temporary instrumentation and the attempted behavior changes were removed.
