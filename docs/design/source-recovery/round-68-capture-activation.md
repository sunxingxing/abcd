# Round 68: guarded capture activation across normal CFG blocks

Baseline: `95ef0813`. This increment adds an original-flow activation proof to
normal-owner guarded deferral. It changes neither the requirement lattice nor
the source-language contract. Exceptional parents and unguarded requirements
retain their prior admission. No declaration is moved after its tree freezes.

## Evidence before implementation

The four first refusals were `CaptureCellError=3 > CaptureError=1 >
ScopeError=3`. All creators are normal `func_main_0` methods without try regions.
All requests are ordinal 0, `CaptureDomain::kGuarded`; the child's original
load is depth 1, rebased request depth 0. The original CFG, not disassembly
order or paired source, supplies the block and dominance evidence.

| Record | Creator | Capture site | Child load / named guard | Cell | Allocation | Unique direct store |
|---|---|---|---|---|---|---|
| o3/55292 WlanSettingItem | m55546 | definemethod PC221, B5 | m55847 PC20 / 22 | slot0, WlanSetting | PC0, B0 | PC280, B5 |
| o10/52872 RadioButton | m53321 | defineclass PC100, B5 | m53405 PC22 / 24 | slot0, compRadio1 | PC0, B0 | PC158, B5 |
| o29/12767 WrapBuilderAsField | m13253 | defineclass PC119, B5 | m13193 PC22 / 24 | slot1, Child | PC0, B0 | PC263, B5 |
| o21/12185 ComputedInitParam | m12777 | definemethod PC311, B8 | m12661 PC23 / 25 | slot1, Child | PC0, B0 | PC719, B14 |

The first three have B0 branch/merge B3, then another branch/merge B5.
B0 dominates B3, which dominates B5. o21 continues through B5 -> B8 -> B11
-> B14 dominance, with intervening branches. All target slots have one
original direct store. The later class definitions occur at PC253, PC131,
PC154 and PC692 respectively. These are outer/later class bindings, not a
blanket class-self initialization premise. Two actual capture sites are
method definitions; naming every request as a class-creation site was imprecise.

The missing evidence was allocation activation, not a new exception protocol:
the existing same-block rule prevented rebasing an early guarded capture
although the original allocation dominates both its creation and store.

## Proof and integration

`LocalAccessPlan` retains the entry/exit intervals from normal CFG dominance.
The existing acyclic and cyclic-initialization analyses populate both; a
cyclic method with local storage but no unclosed local read computes the same
shared dominator analysis for activation only. It does not add initialized
read facts. All vectors and dominance scratch are charged before allocation.

`NormalScopeFacts::Rebase` first uses all existing rules unchanged. Only if
initialization remains unproved, a Guarded requirement may use the new proof:

- the binding resolves to the original environment's local cell;
- all closure writes have the existing Language premise;
- the target cell has exactly one original direct store;
- the allocation dominates both creation and that store, and precedes each
  when in the same block.

The unique-store rule conservatively rules out direct resets/reinitializations.
The original input and family write checks still reject Hole writes and
unclosed strict descendant obligations. `SourceAccess`, `Language` and
`Defined` do not receive this new fallback, even under the source contract.
An unknown closure writer still refuses. This is an implementation boundary,
not a claim that all multiple-store source programs are invalid.

Binding and round-43 fulfilment keep the real storage/request identities.
`MarkDeferredCells` and `VerifyDeferredCells` independently rebuild working-IR
flow/dominance once per invocation and verify allocation -> unique store.
The retained deferred table is charged separately from live dominance scratch;
family preparation retains that table's charge after marking.

Dominance alone does not authorize publishing a declaration: FunctionTree
still requires the store's declaration in the allocation's own draft/scope,
and TreeFlow independently requires it directly in that same BlockStmt.
Original-flow/recovery keep the original store origin and cell binding. Thus a
loop with an outer allocation cannot silently become a per-iteration `let`,
and a freshly allocated environment on every iteration keeps distinct bindings.
The guard remains a throwing read wherever initialization is unproved. There
is no conversion to an initialized `kCellGuard`, no guard deletion, no new
catch staging and no change to declaration recovery/printer passes.

The shared original analysis and independent working analysis are bounded by
the existing O((V+E) log V) dominator algorithms. Rebase adds logarithmic block
queries. Working checks scan each cell's disjoint access range once; duplicate
requests reuse its marked entry. Memory is O(V+E+cells), pre-admitted within
existing method/record limits; no per-cell graph walk or cap increase.

## Tests and boundary

- `capture_activation_test`: real decoded normal CFG with branches/copies,
  200/201/400/800 blocks/requests, new-environment loop activation, Guarded
  admission, strict-domain/unknown-writer refusals, reset/non-dominating
  allocation/forged store mutations. Exact original-local and capture-plan
  byte/work boundaries.
- `class_deferred_test`: independent working dominance with many distinct
  cells and split blocks, exact Mark/VerifyDeferredCells budgets, valid-id
  non-dominating storage mutation, and existing tree/original binding,
  declaration and activation mutations.
- `capture_activation_cli_test`: real DevEco es2abc class fixtures, both
  contracts, early TDZ, throwing initialization, branch/copy equivalents,
  later class definition, and retained readers from distinct loop iterations.
  Node compares exception constructors and complete traces, not messages or
  stacks. Declaration-hoisting and shared-activation mutations must be detected.
  See [fixture provenance](../../../tests/integration/fixtures/capture_activation/README.md).
- Existing `guarded_captures` branch fixture stays refused with no source:
  allocation dominance is now proved, and the next first chain is
  `EvaluationError::kUnsupportedOperation > EvaluationObligation::kPlan`.
  The test asserts that full chain instead of the stale ScopeError leaf.
  Child-write/forward-write/own-write negative assertions are unchanged.

Two initial for-loop fixture variants encountered the existing
`EnvironmentFlowError::kAmbiguousJoin` at environment copying on a backedge;
this increment does not repair that environment-join analysis. The accepted
while-loop fixture has a verified fresh allocation/pop each iteration and
executes the required activation differential. No claim is made that all loop
lowerings or conditional-declaration scopes are now supported.

## Acceptance

Both exploration runs against explore67c/explore67cs: **516 -> 520/620** and
**556 -> 560/620**. Each run changes exactly the four listed records, all to
complete. No status regression; every previously complete/partial output is
byte-identical, including o32/49868. Eight new outputs pass `node --check`.
Cohort output and comparisons: `/private/tmp/r68/cohort/`,
`/private/tmp/r68/cohort.log`. No holdout, full test suite or RSS benchmark run.
Full keep-going build passed, including a final rebuild after the additional
boundary tests. Fixed regression: 65/65. New/directly related tests: 6/6;
24 Node comparisons and eight detected mutations. Targeted
`capture|deferred|guarded|class|local_initialization|cyclic_cells|scope`: 59/72,
with all 13 failures in the existing status.md list. CLI/corpus/failure/receipt/
manifest sweep: 61/62, only the pre-existing family_source_cli_test failure.
No existing failure assertions were changed except the documented branch leaf.
Logs: `/private/tmp/r68/acceptance.json`, `final-build.log`, `final-new.log`,
`boundary-tests.log`, `targeted.log`, `sweep.log`.

A fresh read-only implementation review found no blocker. Its test gaps were
closed with independent working-CFG non-dominance, verifier/local-cache exact
budgets, and a loop mutation that preserves the early TDZ while sharing later
values. Changes remain uncommitted for cc review.
