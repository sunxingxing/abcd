# Round 64: internal inputs and cyclic cell initialization

Baseline `3a8b010e`, `explore63` / `explore63s`. No source premise was added.
The diagnostic does not claim that a top/unknown domain is definitely Hole.
No guard-classification rule is changed. New local initialization facts must
pass the existing initialized-guard verification; the class-self guards below
remain refused.

## Evidence before admission changes

`InternalValueEvidence` is a fixed-size, trivially copyable record. Before Value
IR exists it names a semantic `InstructionId`, `SsaValueId`, and read ordinal,
not a fabricated `OpId` or `UseId`. Its producer is the actual SSA entry,
instruction or phi; copies stay copies. PC provenance stays in the existing
method/original-PC/working-PC envelope. Local-cell failures occur before value
analysis: they name the real cell, allocation and observed store, without
inventing an SSA read or asserting an inferred marker domain.

Both publication paths, route receipt serialization/parsing, `abc_semantic
--record-source`, and omitted class members retain the evidence and typed
`InternalValueReason` in the cause chain. Existing error enum values and
resource classifications are unchanged.

| Target | Original / working failure PC | Before-fix sub-reason | Producer / observer evidence |
|---|---|---|---|
| o0/4660, m5040 | 12 / 12 | Mixed (6), unknown domain | captured load instruction 5 @10, SSA14 -> read 0 of HoleGuard instruction 6 |
| o20/8166, m8556 | 13 / 13 | Mixed (6), unknown domain | captured load instruction 5 @11, SSA22 -> read 0 of HoleGuard instruction 6 |
| o32/40109, m40474 | 129 / 129 | CyclicCellInitialization (8) | env0/slot0, allocation instruction0 @0; previously seen store25 @59; load51 |
| o33/26349, m26657 | 132 / 132 | CyclicCellInitialization (8) | env0/slot0, allocation instruction0 @0; previously seen store48 @130; load49 |
| o8/10284, registerListener m12733 -> m11580 | 49 / 49 | Mixed (6), unknown domain | external module load instruction19 @47, SSA53 -> read0 of copy instruction20; creation m12733 @224 |
| o20/10622, aboutToAppear m11067 | 633 / 633 | CyclicCellInitialization (8) | env0/slot0, allocation instruction0 @0; previously seen store4 @10; load238 |

The two static-initializer cases are confirmed class-self reads: entry m4850
stores the class created at PC13 into the fresh self cell at PC28, creates
m5040 at PC40 and invokes it at PC51; entry m8392 allocates the class scope at
PC32, creates the class at PC39, stores it into slot0 at PC54, creates m8556 at
PC250 and invokes it at PC261. `ReadClassInitializerFunction` still reads these
with `InputCaptureMode::kClosed`, so no capture requirement is discharged.
Repairing their creator-to-initializer binding is separate from this round's
largest local group. The KeyboardController omission is an unguarded external
module value and remains strict.

## Local proof and independent verification

Only a cyclic local-read obligation that the existing proof cannot close asks
for the additional dominator certificate. Successful existing methods keep
exactly their previous local facts; this is not a tree-verifier retry.

For the same environment allocation A, store S and read R, require strict
`A dominates S dominates R`, including instruction order within a basic block.
This proves initialization after the most recent dynamic A: any path from a
repeated A to R bypassing S would contradict S's dominance (the entry-to-A
path can avoid S since A dominates S). It does not move, combine or reallocate
source declarations. Every store input must still pass the existing semantic
Language check; an explicit Hole store remains rejected. Unknown transitive
closure writes are not solved around cycles: require the existing Language
write premise or no unpreserved creation.

One normal dominator tree plus sorted disjoint per-cell dominator intervals
costs O((V+E) log V + N log N), with O(V+E+N) charged storage. All new vectors
are admitted before reserve. Dominator scratch is included while alive. No
per-cell CFG walks, opcode template, register/PC admission, or same-block
extension was introduced. The new helper is private to original-input analysis.

Working IR independently uses `BuildCellInitializations` /
`VerifyCellInitializations`: it re-derives the allocation and actual same-cell
store and checks both dominance relations over original live operations. The
new input certificate does not bypass evaluation, tree scope, original flow,
or recovery. A forged initializing operation is rejected by that verifier.

## Results and validation

Final cohort comparison: previous flags **509 -> 510/620**, source flags
**547 -> 549/620**, no status regressions and no text change to any previously
complete record. Only these six targets plus o24/37520 change diagnostics.

| Target | Previous flags | `--language-source` |
|---|---|---|
| o0/4660 | Same refusal, Mixed evidence m5040 @12 | Same |
| o20/8166 | Same refusal, Mixed evidence m8556 @13 | Same |
| o32/40109 | Original local refusal gone; m40502 @409, PhasedMethodError::kControl > ExceptionControlError::kMultipleExits, Try0 handler434 | Same |
| o33/26349 | Original local refusal gone; m26959, DerivedProjectionError::kProtocol, no PC provided | Complete |
| o8/10284 | Still partial; omitted child m11580 @49 carries Mixed evidence | Same |
| o20/10622 | Complete | Complete |

The remaining o32/40109 close-region exits are original PC432 ->436 and
PC423 ->445, preserved by typed region evidence. For-of remains parked.
o24/37520 remains refused under both flags: preparing an earlier member now
reaches m38333, EvaluationError::kUnsupportedOperation / kPlan without a PC,
before m38116's old DecisionPrefix failure. No unlocated PC was fabricated.
All three new complete flags/record outputs pass `node --check`.

The final build succeeds and fixed CTest preset is **65/65** (168.40 seconds).
New tests plus diagnostic paths: **6/6**. Related cell/lexical/semantic/class/
capture/guarded/recovery/source-route tests: **64/78**, with 13 already-listed
failures and `async_semantics_test`. The latter was rebuilt and reproduced at
baseline `3a8b010e` in `/private/tmp/r64-baseline` (same `bad_variant_access`,
exit 134); its assertions were not changed. CLI/corpus/failure/receipt/manifest
sweep: **54/55**, only the already-listed `family_source_cli_test` fails.

Targeted failures: class_tree_source_test, unguarded_derived_cells_source_test,
unguarded_literal_closure_source_test, unguarded_derived_literal_receiver_source_test,
captured_receiver_source_test, unguarded_captured_receiver_source_test,
unguarded_forwarded_receiver_source_test, reversed_captured_receiver_source_test,
unguarded_reversed_captured_receiver_source_test, unguarded_reversed_forwarded_receiver_source_test,
reversed_initialization_captured_receiver_source_test,
unguarded_reversed_initialization_captured_receiver_source_test,
unguarded_reversed_initialization_forwarded_receiver_source_test, async_semantics_test.

Commands/logs (temporary local evidence):

- Full `cmake --build build/regression -j 6 -- -k`: `/private/tmp/r64-build-delivery.log`, success.
- `ctest --preset source-regression -j 1`: `/private/tmp/r64-fixed.log`.
- `ctest --test-dir build/regression -j 1 -R 'internal_value|cyclic_cells|cell_plan|lexical|semantic|class|capture|guarded|source_route|tree_failure|record_failure|recovery' --output-on-failure`: `/private/tmp/r64-targeted.log`.
- `ctest --test-dir build/regression -j 1 -R 'cli|corpus|failure|_receipt|manifest' --output-on-failure`: `/private/tmp/r64-sweep.log`.
- Six-test final subset: `/private/tmp/r64-new-tests.log`.
- Both final cohorts: `/private/tmp/r64-final-cohort.log`, `/private/tmp/r64-final-cohort/explore64{,s}`.
- Before-fix six-case evidence: `/private/tmp/r64-evidence/o*/manifest.json`.

200/201/400/800 direct-load cases have exact minimum budgets 173600 / 174418 /
342320 / 679760 bytes and 7668 / 7705 / 15068 / 29868 work units. Copy and
block-split variants are also checked at their own exact boundary; minimum
budget minus one must refuse. These are bounded pass measurements, not a
full-corpus RSS benchmark.
The expected target gain includes recovery of the formerly omitted
`aboutToAppear`. Completing that partial class also allows the pre-existing
class-declaration proof to replace its incomplete `let X = class` wrapper.
This is reported as a targeted partial-to-complete change, separately from the
byte-for-byte comparison of previously complete output.

The real es2abc fixture and independent Node oracle are documented in
`tests/integration/fixtures/cyclic_cells/README.md`. The unit test covers
200/201/400/800 loads with copies and block splits, exact byte/work boundaries,
uninitialized and reallocated cells, explicit Hole stores, independent witness
mutation, all diagnostic domains, SSA entry/phi/instruction identities, PC0,
normalized working PC, missing facts, receipt and both JSON paths.
