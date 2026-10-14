# Round 67 II: requirements and creator-storage foundation (incomplete)

Historical boundary committed as `640dd19f`, based on `97ca420a`. That increment did **not** complete II or III.
The subsequent [closed descendant production slice](round-67-closed-descendants.md)
adds publication for closed, non-derived classes; the broader requirements and
declaration work described below remains open.

At the boundary documented here:
The production descendant preparation gate remains unchanged. No class-body,
owner-range, name-reservation, enclosure or write-barrier permission is inferred
from a requirement summary. There are no new source-language premises.

## Implemented boundary

- `ClassRootBinding::kRequirements` reads and residual-prepares class member
  families without binding their external root against an incomplete creator.
  `FamilyPreparation` records this state; `PreparedFamily::Finish` refuses it
  with the appended `FamilyPrepareError::kClassRequirements`. `BindCreator`
  changes the state only after binding and independent verification. A failed
  rebind returns the object to requirements-only state.
- `CollectCreatorClassDemands` retains occurrence, creator, input/IR revisions,
  original creation point and the member family's forwarded use/domain/source.
  It resolves actual member/constructor/defined-member contexts, not raw cells
  or source names. O(N log N) collection charges its output before allocation.
- `PlanFamilyCells` accepts occurrence-indexed external requirements. A counted
  per-creator distribution feeds the existing reverse-preorder requirement join;
  pure forwarding never fabricates a lexical instruction. `CellPlan` resolves
  typed class creation at the same working environment boundary as function
  creation. Construction and verification use the same storage identity query.
- The actual `CaptureStorage` is carried through class-root binding, independent
  rebinding, deferred-failure diagnostics, `ExternalFamilyCapture`, completion,
  and `ClassRootWriteInput`. No caller is allowed to substitute an incoming
  OuterCell for a creator ParentCell during the new binding transition.

These entry points are exercised by tests, but are not installed as a production
nested-class scheduler. Existing callers use their original bound preparation
path and null storage default. The old gate is intentionally retained.

## Remaining dependency

`FamilyPreparation::Read` still calls `FamilyTreeInput::Prepare` immediately.
The full task needs an owned preparation graph whose vertices retain raw
family occurrences before residual preparation and external binding, so that:

1. an explicit stack discovers all class/member families without recursive
   `PrepareClassTree -> FamilyPreparation::Read` descent;
2. default/residual revisions settle and summaries propagate bottom-up across
   both ordinary-function and class-family edges;
3. full owner ranges and nested free-name reservations are fixed;
4. each creator's real storage and deferred declarations settle before its AST;
5. class bodies finish under that creator's CaptureTree, then the creator's
   semantics and every class write barrier are independently rechecked.

The current helper cannot safely replace that graph by recursively finishing
classes, by temporarily inventing owner ranges, or by moving declarations after
construction. `FamilyWrites::Classes` retains its existing root restriction;
`ReferenceNames` and production class completion have not been generalized.
Creator default-body view propagation is also not claimed by the storage-only
interface. These are required work, not a new admission policy.

## Test scope

`creator_requirements_test` uses real DevEco/es2abc fixtures with a creator that
has no direct capture access. Both previous and source contracts are tested.
Without demanded creator storage the external class binding rejects; with the
verified storage it succeeds. Tests cover wrong occurrence, wrong demand owner,
wrong creation operation, stale storage revision, and the Finish gate.
Width and member-family forwarding depth are 200/201/400/800. Exact byte/work
boundaries are checked for collection, family cell planning and external binding.
The depth fixtures read the entire member family, but their final binding has
constant-sized input; its budget is not the total family-read budget.

No executable descendant-class output, Node differential, loop activation,
self/outer-binding completion, parent setter, throwing constructor, nested-class
completion, or final enclosure/barrier mutation acceptance is claimed here.

## Cohort evidence

Against explore67/explore67s, the final workspace build retains **513/620** and
**552/620**. All 80 manifests are byte-identical; every existing complete/partial
source is byte-identical, including o32/49868. No diagnostic changed. Evidence:
`/private/tmp/r67ii-final-cohort/comparison.json` and
`/private/tmp/r67ii-final-cohort.log`.

| Record | Creator method | Original / working PC | Current first refusal |
|---|---:|---:|---|
| o18/52728 | 53236 | 179 / 179 | FamilyFileError=1 > FamilyCreationCheck=14 |
| o18/59564 | 60156 | 232 / 232 | FamilyFileError=1 > FamilyCreationCheck=14 |
| o18/60829 | 61449 | 126 / 126 | FamilyFileError=1 > FamilyCreationCheck=14 |
| o18/63861 | 64425 | 46 / 46 | FamilyFileError=1 > FamilyCreationCheck=14 |

These are the unchanged increment-I preparation gates, not completed increment-II
or source-publication outcomes. No new output requires `node --check`.

## Validation of this foundation

- Full `cmake --build build/regression -j 6 -- -k`: passed.
- `ctest --preset source-regression -j 1`: 65/65.
- New/extended creator and family-creation tests: 5/5. The new test runs 18
  real-ABC cases (nine per contract), including the exact boundary checks.
- `class|capture|deferred|guarded|family|creation|recovery`, excluding the four
  creator/family-creation tests already run above: 65/79. All 14 failures are
  the registered existing failures, identical by name to increment I.
- `cli|corpus|failure|_receipt|manifest`: 58/59, only the registered existing
  family_source_cli_test failure. No stale expectations were changed.
- `git diff --check`: passed.

Commands/results are in `/private/tmp/r67ii-acceptance.json`, with logs
`/private/tmp/r67ii-accept-{build,new,fixed,targeted,sweep}.log`.
Boundary measurements are in `/private/tmp/r67ii-scale.log` (byte/work pairs).
No holdout, full suite, RSS benchmark, commit, push or stash was performed.
