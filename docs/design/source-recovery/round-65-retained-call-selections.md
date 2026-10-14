# Round 65: retained fallback arguments

Baseline: `3cb1e433`. No source-language premise added. The investigation uses
ABC instructions, raw Value IR and final evaluation plans; no paired source.
Temporary diagnostic objects/binaries were linked in `/private/tmp/r65-evidence`
without modifying the production library or checkout for instrumentation.

## Evidence before behavior changes

Both explore64 flag sets have the same four record-entry failures:
RecordProgramError=6 > FunctionTreeError=1 > ExpressionError=4 >
CallPlanError=4 > CallReferenceError=1 > EvaluationKind=1 > TemporaryReason=4.
All four are **normal**, not phased. Original and working PCs are identical.

| o21 record / entry method | callee property Op@PC | child call Op@PC | truth / branch PC | selected phi / join | final call Op@PC |
|---|---|---|---|---|---|
| 15203 / 15510 | 124@354 | 147@412 | 419 / 422 | V22 / B8 | 156@434 |
| 16320 / 16835 | 125@359 | 148@417 | 424 / 427 | V24 / B8 | 157@439 |
| 18179 / 18713 | 127@361 | 150@419 | 426 / 429 | V21 / B8 | 159@441 |
| 23526 / 23982 | 116@311 | 137@361 | 368 / 371 | V24 / B8 | 146@383 |

In every row the callee is saved in B5 and consumed in B8. The child call is
already `kPredicateSnapshot`: its first observation is the ToBoolean input;
its other observation is the original keep phi input (edge 10). The fallback
is an earlier call result in B5, supplied on edge 9 from B6. The keep path is
B7. Respectively, fallback values/producers are V114/Op119@337,
V116/Op120@342, V113/Op122@344 and V113/Op111@294. The callee has exactly its
call's callee use; its `kRoot/kCrossRegionValue` is not an alias ambiguity.

A source-level reconstruction with anonymous names is:

```js
const saved = earlierCall(...);
receiver.member(earlierArg, key, child(childArg, keyAgain, saved) || saved);
```

`earlierArg` and `childArg` may be distinct property reads. They must not be
merged, nor may the member lookup move after the child. The child result is
observed by the predicate and retained edge but computed once.

`FindLogicalRegions` already discovers each raw region. However,
`FindSelectionExpressions` requires its logical RHS to be an arm-local
`kPhiInput`; these fallback definitions are in B5. Conversely,
`FindConditionalExpressions` proves both arms as `SelectionSnapshot`, then
skips this conditional in favor of the logical owner. Thus **neither path
registers the region**, and `BuildLogicalValues` has no selected interval to
bridge B5 to B8. `VerifyCalls` correctly refuses the un-inlined property.

This is not the deferred Round-46 consumer eligibility extension: the selected
phi has one ordinary argument consumer, and the child already has a verified
predicate snapshot. No new predicate-plus-keep consumer is admitted.

## Local change and proof boundary

`ReferenceArguments` indexes unmet member References and their existing eager
argument trees. The original placement view is rebuilt by the independent
verifier after fusion; no symbols, names or owner numbers are allocated.
Only such an argument may retain the already-proved conditional when both
arms are saved values. Ordinary logical RHS producers retain the original
logical owner, and non-member uses retain their prior plan.

The existing `Domain::Build` reverse interval proof and `Domain::Verify`
forward proof consume callee, preceding arguments, predicate snapshot,
selection, subsequent arguments and invocation. Existing `ProjectSelectionLogicals`
may represent this same conditional with native `||`/`&&`; its original truth
and phi evidence remains attached. Consumer eligibility is unchanged.

TreeFlow independently reconstructs the selection and checks the existing
snapshot assignment/retained arm and original selected edges. Tree operands
check exact snapshot identity, member base/receiver and call arguments.
OriginalFlow and source-variable recovery retain original events and exception
exits. No verifier relaxation, printer change, catch staging or declaration
movement is introduced.

The two new byte indexes are admitted before allocation. Each operation and
inline edge is visited once; pool/name resolution is not involved. Overall
complexity stays bounded by the existing control/SSA analyses, O(N log N).
The seed index follows existing forward eager-inline edges; it does not add a
new operation or block-shape admission predicate.

Boundaries: two sibling retained selections can still need another composition
proof (`twice.abc`); a jump corridor between the selected join and the final
consumer still meets the existing SelectionConsumer cross-block limitation.
This round's successful split variants split the linear callee-to-argument
prefix. Neither limitation is a language invariant. The remaining record
16320 refusal is not expanded in this round.

## Tests and acceptance

`retained_call_selection_test` checks copies/register shifts/linear block splits,
non-member non-seeding, exact consumer/value/phi correspondence mutations,
missing/duplicated tree events, wrong truth semantics, stale original-flow
identity, and successful independent tree/recovery verification. Scale tests
also build and verify trees and recovery at 200/201/400/800:

| repetitions | evaluation minimum bytes | work |
|---:|---:|---:|
| 200 | 8,044,343 | 1,979,288 |
| 201 | 8,084,523 | 1,989,172 |
| 400 | 16,080,343 | 4,009,360 |
| 800 | 32,152,343 | 8,122,212 |

The exact byte and work bounds pass; one less fails with the existing resource
or work class. The real es2abc fixture is documented in
`tests/integration/fixtures/retained_call_selection/README.md`: 104 Node
comparisons, 12 detected mutants, two typed sibling-selection refusals.

Both complete cohort comparisons against explore64/explore64s:

| flags | before | after | status regressions | old complete text changes |
|---|---:|---:|---:|---:|
| numeric + constructors | 510/620 | 513/620 | 0 | 0 |
| language-source | 549/620 | 552/620 | 0 | 0 |

Only the four target records change diagnostics in either flag set:

| record | result, both flags |
|---|---|
| 15203 ComputedProperty | complete |
| 16320 ComputingPropertyResolution | original entry refusal gone; m17082@171, call Op70 / property Op8, CallReferenceError::kNotInline > Root > CrossRegionValue |
| 18179 CustomComponentUse | complete |
| 23526 ObservedV2ClassUser | complete |

All six newly complete flag/record files pass `node --check`. Exports and
comparison log: `/private/tmp/r65-first-cohort/` and
`/private/tmp/r65-evidence/cohort-first.log`. Final build/CTest results are
recorded in status.md after completion. No holdout, full test suite or RSS
benchmark; no commit/push/stash.
