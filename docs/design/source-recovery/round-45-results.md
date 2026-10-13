# Round 45 workspace evidence

2026-10-14; baseline `fa9dfbd9`; uncommitted. No holdout or RSS run.

## Scope and implementation

- `EvaluationFailure` preserves the existing error numbers and resource/work
  classes. Fixed-size witnesses carry the failing obligation, operation,
  block, use, placement, temporary reason and actual consumer. Original and
  working PCs come from input origins; absence is distinct from PC 0.
- Logical-input, coverage and snapshot failures retain the final failed
  attempt, including assignment fallback. An absent logical consumer is
  refused at the selected branch instead of dereferencing an empty optional.
- Family/record-entry JSON and standalone preparation retain the typed leaf;
  `abc_semantic --record-source` agrees with the manifest.
- `EmitBlock` and `EmitDecisionBlock` share `EvaluationDisposition`.
  Initialized `kCellGuard` produces only a boundary anchor. A real throwing
  `kGuardedCellRead` cannot use that disposition. Existing independent
  tree/recovery checks re-prove the original initialized guard and boundary.
  No changes to control joins, follows, declarations or catch staging.
- Tree ownership diagnostics now separate `decision_entry` from the actual
  consumer's `expected_owner`. No symbol or owner is allocated for an anchor.
  The new classification is O(1) per operation with no new allocated index.

## Seven T targets

Before changing emission all seven reported `kMissingExpression` with
`kCellGuard`, never `kGuardedCellRead`. The old failure locations are below;
PC columns are original/working. Results are under `--language-source`.

| Record | Old method / Op / PC | Final first result |
|---|---|---|
| o8/10284 KeyboardController | 12077 / 196 / 477/477 | EvaluationError::kOwnership > kSnapshotCandidate, m12516 Op24 block2 PC59/59; actual consumer Use34, owner Op33 PC82/82 |
| o19/7617 AudioInputDeviceManagement | 7958 / 33 / 88/85 | complete |
| o19/12741 AudioInputDeviceSwitcher | 13192 / 33 / 88/85 | complete |
| o19/22760 AudioOutputDeviceManagement | 23270 / 33 / 88/85 | RecordProgramError::kResourceLimit > TreePrintError::kResourceLimit |
| o19/28925 AudioOutputDeviceSwitcher | 29433 / 33 / 88/85 | complete |
| o19/35025 VolumeManagement | 35440 / 33 / 88/85 | RecordProgramError::kResourceLimit > TreePrintError::kResourceLimit |
| o34/10794 renderer | 11425 / 33 / 87/84 | RecordProgramError::kResourceLimit > TreePrintError::kResourceLimit |

Print-budget failures are record-wide and have no method/PC witness; none is
invented. Under previous flags o8 reaches the same new snapshot refusal; the
other six retain their earlier capture refusal.

## Eight existing E targets

All still refuse with `EvaluationError::kOwnership` (5). The full published
leaf chain is that enum followed by the obligation below. No E admission is
changed. All these PC pairs have equal original and working values.

| Flags | Record | Method | Op / block / PC | Obligation |
|---|---|---|---|---|
| both | o5/27642 StyleUtils | 27869 | 113 / 12 / 276 | kLogicalInput (4), absent consumer |
| source | o7/13863 GridLayout | 14290 | 201 / 12 / 508 | kLogicalCoverage (17); Use276, owner Op245 block15 PC633 |
| both | o18/22901 Tcp2TwoWayTls | 24302 | 45 / 3 / 116 | kLogicalInput (4), absent consumer |
| both | o18/38531 TwoWayTls | 39900 | 45 / 3 / 116 | kLogicalInput (4), absent consumer |
| both | o23/4775 Index | 5424 | 28 / 10 / 58 | kLogicalInput (4), absent consumer |
| both | o28/10155 KeyboardController | 11832 | 24 / 2 / 59 | kSnapshotCandidate (16); actual Use34, owner Op33 PC82 |
| both | o32/28894 assertClose | 29074 | 65 / 3 / 174 | kLogicalCoverage (17); Use111, owner Op101 block11 PC258 |
| both | o32/41871 isPromiseLike | 42137 | 13 / 3 / 26 | kLogicalInput (4), absent consumer |

## Validation boundary

Full keep-going build passed. Serial fixed set passed **65/65**.
Final cohort exports in `/private/tmp/r45-delivery-cohort`, compared against
`explore44` / `explore44s`, are **486/620** and **520/620**: zero status
regressions and zero byte drift for already-complete records. The three gains
are o19/7617, 12741 and 28925. See `/private/tmp/r45-delivery-cohort.log`.

New `decision_guard_test` covers initialized anchors versus throwing reads,
removed/duplicated/expression/incorrect-boundary anchor mutations, removed
initialization premises, copies and split blocks, sizes 200/201/400/800,
and exact byte/work budget boundaries. `tree_failure_test` covers all new
obligations, PC 0/absence/synthetic/normalized locations, both JSON routes and
unchanged resource classes. `evaluation_failure_cli_test` checks all eight
real E records and agreement with `abc_semantic`. Standalone preparation
round-trip coverage is in `class_tree_context_test`.

CC review corrected the finally-throw oracle: ABC m473 PC170 dispatches to
H247, PC325 to H337, and PC377 propagates to the caller. The repeated finalizer
trace is faithful to es2abc's protected ranges, not an engine bug. Only this
mode uses the explicit bytecode-derived expected result; the other modes
continue to use the independent JS oracle. Handler edges and the full trace
are asserted. Mutation detection uses only unmutated-agreeing modes. See
[fixture details](../../../tests/integration/fixtures/decision_guards/README.md).
Round 44's guarded_calls CLI has no finally-throw mode and is unchanged.

The stale o5/28303 ExternalRoot assertion is updated to its real first refusal
since Round 43: m28799, original/working PC677,
PhasedMethodError::kInput > SemanticError::kUnsupportedInstruction. The test
checks the exact chain, method, PC, decoded boundary, and CLI/manifest agreement.
The now-complete o19/12741 ownership assertion was removed; new IR coverage
checks that obligation directly.

## Review correction verification

Only tests and documentation changed in this follow-up. The seven-test serial
slice passed **7/7**: `class_tree_context_test`, `decision_guard_test`,
`decision_guards_cli_test`, `guarded_calls_cli_test`, `tree_failure_test`,
`evaluation_failure_cli_test`, and `tree_failure_cli_test`.
The decision fixture passed all 22 source-oracle comparisons, both strict
ABC-handler expectations, and all six mutation checks.
Log: `/private/tmp/r45-review-targeted.log`.

The requested serial fixed-set rerun passed **64/65**. The only failure was
`rich_text_record_source_test`: its worker invocation failed with empty stderr
(the full test took 65.12 seconds). Without any code/test changes, its isolated
serial retry passed **1/1** in 13.76 seconds. This is not reported as a fresh
65/65 full run. Logs: `/private/tmp/r45-review-fixed.log` and
`/private/tmp/r45-review-rich-text.log`. `git diff --check` passed.

## Pre-review targeted run (historical)

Command: `ctest --test-dir build/regression -R "(control|decision|guarded|exception|recovery|evaluation|tree_failure|source_route)" -j 1 --output-on-failure`.

**44/56 passed before the review corrections**. Ten failures are on the existing
failure list. The stale ExternalRoot expectation and incorrect finally-throw
source oracle are corrected above; this table records the earlier run.
The latter completed 24 differential executions (22 agree, two finally-throw
mismatches) and detected six mutants using baseline-agreeing modes only.
Logs: `/private/tmp/r45-final-build.log`, `/private/tmp/r45-delivery-fixed.log`,
`/private/tmp/r45-final-targeted.log`.

| Test | Result |
|---|---|
| source_route_cli_test | pass |
| control_terminal_test | pass |
| control_decision_test | pass |
| exception_plan_test | pass |
| control_composition_test | pass |
| exception_copies_test | pass |
| recovery_flow_test | pass |
| control_view_test | pass |
| recovery_projection_test | pass |
| evaluation_plan_test | pass |
| argument_evaluation_test | pass |
| decision_cells_test | pass |
| decision_prefix_test | pass |
| module_evaluation_test | pass |
| control_plan_test | pass |
| source_route_test | pass |
| phased_decision_module_source_test | pass |
| decision_module_source_test | pass |
| decision_prefix_source_test | pass |
| decision_selection_source_test | pass |
| vertical_decision_module_source_test | pass |
| unguarded_derived_cells_source_test | fail: documented pre-existing |
| unguarded_literal_closure_source_test | fail: documented pre-existing |
| unguarded_derived_literal_receiver_source_test | fail: documented pre-existing |
| unguarded_captured_receiver_source_test | fail: documented pre-existing |
| unguarded_forwarded_receiver_source_test | fail: documented pre-existing |
| unguarded_reversed_captured_receiver_source_test | fail: documented pre-existing |
| unguarded_reversed_forwarded_receiver_source_test | fail: documented pre-existing |
| unguarded_reversed_initialization_captured_receiver_source_test | fail: documented pre-existing |
| unguarded_reversed_initialization_forwarded_receiver_source_test | fail: documented pre-existing |
| decision_cells_paired_test | pass |
| decision_cells_source_test | pass |
| exception_tree_source_test | pass |
| control_composition_source_test | pass |
| exception_outer_source_test | pass |
| shared_decision_source_test | pass |
| phased_shared_decision_source_test | pass |
| phased_shared_decision_refusal_test | pass |
| decision_snapshots_source_test | fail: documented pre-existing |
| home_decision_prefix_source_test | pass |
| control_transfer_test | pass |
| exception_arguments_test | pass |
| decision_guard_test | pass |
| decision_guards_cli_test | fail: new finally-throw oracle |
| guarded_call_test | pass |
| phased_guarded_reads_test | pass |
| guarded_reads_test | pass |
| exception_dispatch_test | pass |
| decision_continuations_cli_test | pass |
| guarded_calls_cli_test | pass |
| phased_guarded_captures_cli_test | pass |
| guarded_captures_cli_test | pass |
| tree_failure_test | pass |
| evaluation_failure_cli_test | pass |
| tree_failure_cli_test | fail: stale baseline expectation |
| exceptions_cli_test | pass |
