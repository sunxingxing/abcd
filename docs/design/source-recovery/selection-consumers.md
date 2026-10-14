# Selection consumer boundaries — Round 46

## Implemented scope

Continuation discovery and expression-route eligibility are separate facts.
`IndexSelections` discovers the same continuations as before. `Consumer`, now
`FindSelectionConsumer`, returns a fixed-size typed result: a unique op use,
the pre-existing retained logical predicate/keep use, a required snapshot, or a
materialized value. Failure evidence distinguishes no use, multiple uses, phi
use, cross-block use, protocol role, snapshot and keep-edge identity. Witnesses
retain the actual UseId and original incoming EdgeId; op locations come from
input origins. JSON preserves absent locations and real ID/PC zero.

`BuildLogicalValues` ends a route before a following selection without an
admitted consumer. The preceding route still undergoes reverse construction and
forward event verification. The following selection retains its phi, temporary
and control representation, including all external observations. This is a
pre-construction boundary, not catching an ownership error and retrying it.

`VerifyLogicalValues` independently reconstructs selections, consumers and
continuations from the graph and original placements. It applies the same exit
boundary and then verifies every committed member's consumer and event order.
Child, partial and forwarded selections retain their separate parent/phi
checks; absence from the top-level consumer index is not a refusal for those
children. A forged top-level fusion across a required materialization is refused
with the typed exit witness. Existing complete routes keep their placements.

This increment does not widen consumer eligibility, alter catch state, allocate
symbols, change truth tests or weaken tree/recovery checks. In particular, two
source property reads such as `!x.p ? "default" : x.p` remain distinct reads;
the selected result used by several subsequent operations is evaluated once.

## Deferred step 4: joint truth-event ownership

The experimental extension using `SelectionLogical.value/input/keep_consequent`
to admit a new joint predicate + keep-edge consumer has been removed. The
experiment changed previously successful plans and regressed o8/65146
HardKeyUtils, method 65316 `getHardKeyValue`, at original/working PC 1871,
Op669: `FunctionTreeError::kSemantics > TreeSemanticError::kFlow >
TreeFlowError::kPlacement`. A small `booleanJoin` fixture also exposed a placement
failure. After withdrawal, both return to complete without any TreeFlow change.
The ordinary pre-existing retained-logical consumer remains unchanged.

The first increment alone removes all five targeted missing-consumer refusals;
there is no need to broaden the joint obligation in this round. A future round
must design that obligation together with tree truth-event ownership and a rule
preserving already-valid plans. Do not independently enable new consumer
eligibility and expect the existing tree correspondence to accept it.

## Complexity and verification

The typed result has no dynamic allocation. The existing consumer index prepays
`sizeof(SelectionConsumer)` per operation before allocation. Each admitted
root selection's use chain is examined once per index construction; route
extension remains linear in visited selections. Builder and verifier retain the
existing control analysis bounds, at most O((V+E) log V). No opcode pattern,
selection-count cutoff, new same-block admission or source-language premise is
introduced. Existing local-expression scope limits are unchanged.

`selection_consumer_test` checks typed categories and exact keep-edge identity,
wrong/missing phi uses, snapshot requirements, forged fusion, transparent copies
and split jump corridors. Sizes 200/201/400/800 exercise route construction,
verification, linear work growth and resource/work boundaries. The fixed-size
JSON evidence is round-tripped directly and through both receipt/record paths
by `tree_failure_test`; original resource classifications remain unchanged.

The real DevEco/es2abc fixture and Node oracle cover `!!x && f(x.p)`, `!q && p`,
multiple boolean terms used as an argument, and o23-style property rereads and
multiple uses of a selected value. Getter/call/setter throws, short-circuit order,
rebinding and ToBoolean without `valueOf` are compared by exception constructor,
return values and traces. Member-call composition remains a typed refusal test.
There is no finally-throw mode in this fixture; future such modes must use ABC
handler ranges, not an original-JS finally oracle.

## Acceptance evidence (2026-10-14)

Full keep-going build succeeded with
`cmake --build build/regression -j 6 -- -k`. The final new-test slice
(`selection_consumer_test`, `selection_consumers_cli_test`, `tree_failure_test`)
passed 3/3. `ctest --preset source-regression -j 1` passed 65/65.

Both 40-input exploration runs were compared per record to explore45/explore45s:

| Flags | Before | After | Regressions | Previously complete text changes |
|---|---:|---:|---:|---:|
| numeric + constructors | 486/620 | 488/620 | 0 | 0 |
| language-source | 520/620 | 522/620 | 0 | 0 |

All five first missing-consumer refusals are gone, with identical outcomes under
both flag sets:

| Record | Current result |
|---|---|
| o5/27642 StyleUtils | complete |
| o18/22901 Tcp2TwoWayTls | FunctionTree/Expression/CallPlan reference chain; CallReferenceError::kNotInline, m24302 original/working PC212, call Op93, property Op31, root/CrossRegionValue |
| o18/38531 TwoWayTls | same reference chain, m39900 original/working PC212, call Op93, property Op31 |
| o23/4775 Index | RecordProgramError::kResourceLimit > TreePrintError::kResourceLimit; printer location is not yet published (cluster A, out of scope) |
| o32/41871 isPromiseLike | complete |

HardKeyUtils o8/65146 is complete again under both flags and is byte-identical to
the corresponding baseline. Its source SHA-256 is
`10631a35ce142b8ba30190d738da57992e4876f12cde2e671712b55d25411309`.

`evaluation_failure_cli_test` now explicitly checks the two complete records,
two new reference failures, and the print failure; it keeps the other three
original evaluation-location assertions. Complete source and failure chains
are compared between the manifest and `abc_semantic --record-source`. The old
expectation that StyleUtils remain refused was intentionally replaced, rather
than accepting an arbitrary next result. No TreePrint quota was changed.

Final targeted run:
`ctest --test-dir build/regression -R 'logical|evaluation|selection|decision' --output-on-failure -j 1`
ran 48 tests: 46 passed; `phased_arm_selection_source_test` and
`decision_snapshots_source_test` failed with their already-recorded baseline
failures (see status.md). They were not changed. The updated
`evaluation_failure_cli_test` passed both its isolated rerun and this final
48-test run. No holdout, full test suite or RSS benchmark was run.

## Atomic route coverage (Round 61)

Diagnostic-first investigation at `94e298d8` found the same local mismatch in
o32/28894 m29074 (Op65/PC174 -> Op101/PC258, Use111) and o7/13863 m14290
(Op201/PC508 -> Op245/PC633, Use276). Both registered the selected phi as
`kLogicalValue`, but failed the final consumed-event check. The target use is
owned by `kInitializer` in the existing literal plan. A non-object Domain does
not traverse that initializer's inputs; nevertheless its previous Build result
registered the route. The independently reconstructed route was identical;
there was no predicate/keep-edge joint obligation or exception protocol.

`LogicalCoverageEvidence` now distinguishes ValueMap, RouteMember,
ForwardedRegistration and ConsumedEvent, with fixed-size optional branch/route
IDs and actual registration/coverage state. Existing EvaluationError values,
resource classes, PC provenance and both publication paths are preserved.

The reverse builder counts actual selection occurrences that pass its interval
proof. It may commit the route and its moved members only after every route
selection is consumed. The single-use index and disjoint block traversal ensure
each can be counted at most once. A failed candidate leaves the existing
phi/temporary/literal representation intact; the existing continuation-boundary
rule remains in force. The forward verifier still recomputes coverage from the
original uses independently. No verifier retry, consumer-kind whitelist,
TreeFlow relaxation, literal admission, or declaration change is introduced.
The check adds one scalar counter and O(1) work per visited use; it allocates no
buffer. The existing indexes and charged traversal bounds remain unchanged.

`logical_coverage_test` reproduces the old orphan as a must-reject mutation,
checks all four mismatch kinds, copied/renumbered/split variants, and exact
byte/work admission boundaries at 200/201/400/800. `tree_failure_test` checks
both JSON publication paths, PC zero and absence. The real es2abc fixture in
`tests/integration/fixtures/logical_coverage` compares ordinary, short-circuit,
nested and call-argument selections against Node with full getter/throw traces.
The deferred Round-46 joint predicate/keep-edge admission remains withdrawn.

Round-61 acceptance: full keep-going build passed; fixed set 65/65 serial;
new tests plus JSON routes 3/3; logical/evaluation/selection/decision 52/54
(the registered phased_arm_selection_source_test and decision_snapshots_source_test
failures unchanged); CLI/corpus/failure/receipt/manifest 50/51 (registered
family_source_cli_test failure). Both exploration comparisons against
explore60/explore60s have zero regressions and zero byte changes in previously
complete output: 500 -> 501/620 and 537 -> 539/620. assertClose completes under
both flags; GridLayout completes under language-source only. Its old-contract
DerivedProjectionError at m14399 is unchanged. New complete outputs pass
node --check. The stale evaluation_failure CLI assertions now require complete
source equality between the manifest export and abc_semantic, not an arbitrary
new failure. No holdout, full-suite or RSS benchmark was run.
