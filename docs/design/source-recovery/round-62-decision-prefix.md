# Round 62: DecisionPrefix evidence and local selection consumption

Baseline `fc7b648f`, frozen exploration outputs explore61/explore61s. Both flag
sets contain the same 12 first refusals. Eight are a local tree consumption gap
for verified normal selections; three need phased prefix/event work, and one
is a Try-unit prefix. No phased/try admission changes in this increment.

| Record | Method | First sub-reason before fix | Prefix event: original/working PC | After fix (both flags) |
|---|---:|---|---|---|
| o1/3011 | 3322 | kPrefixArms / kLogicalValue | op189 @493/493 | ControlError::kLoopExits; m5186; no PC |
| o1/12874 | 13317 | kPrefixArms / kLogicalValue | op189 @493/493 | complete |
| o6/5058 | 5417 | kPrefixArms / kRoot (phased) | op21 @50/47 | same refusal with precise event |
| o8/10284 | 11969 | kPrefixArms / kLogicalValue | op114 @284/284 | INCOMPLETE class (status 4; no published leaf) |
| o18/28264 | 29676 | kPrefixArms / kRoot (phased) | op47 @114/111 | same refusal with precise event |
| o18/31094 | 32347 | kPrefixArms / kLogicalValue | op23 @53/53 | complete |
| o18/43021 | 44615 | kPrefixArms / kLogicalValue | op23 @53/53 | complete |
| o20/10622 | 12066 | kPrefixArms / kLogicalValue | op50 @128/128 | INCOMPLETE class (status 4; no published leaf) |
| o24/37520 | 38116 | kPhaseUnit (Try) | Try 8, control 1396 | same refusal with precise event |
| o28/10155 | 11285 | kPrefixArms / kLogicalValue | op114 @296/296 | complete |
| o32/49116 | 49302 | kPrefixArms / kRoot (phased) | op141 @367/362 | same refusal with precise event |
| o38/6590 | 7794 | kPrefixArms / kLogicalValue | op56 @152/152 | complete |

Five records become complete in each flag set, one reaches a later LoopExits
refusal, and two publish explicit partial classes. The latter are NOT counted
as complete: the current partial-class manifest does not publish an omitted
member's typed leaf/location. This diagnostic limitation is outside this fix.
All 12 diagnostic changes per flag set are confined to the target set.

Mechanism and independent verification: [loop-discovery.md](loop-discovery.md#verified-selection-events-in-decision-prefixes-round-62).
Evidence protocol: [failure-evidence.md](failure-evidence.md#decision-prefix-checks-round-62).

## Validation

- Full build succeeded; fixed regression set 65/65, serial.
- `decision|logical|selection|evaluation|tree_failure|recovery|original_flow`:
  59/61; only registered pre-existing `phased_arm_selection_source_test` and
  `decision_snapshots_source_test` failed.
- `cli|corpus|failure|_receipt|manifest`: 51/52; only registered pre-existing
  `family_source_cli_test` failed (owner/origin arena assertion).
- New IR and CLI tests plus diagnostic round-trip tests pass. The real es2abc
  fixture has 1,344 Node comparisons and ten detected mutants. Exact scratch
  boundaries for 200/201/400/800: 13,569,949 / 13,635,762 / 27,125,765 /
  54,237,397 bytes; work: 4,820,569 / 4,844,794 / 9,744,722 / 19,697,078.
- Both flag sets: 506/620 and 544/620; zero status regressions or byte drift
  among already-complete records. All ten newly complete files pass node --check.
- Logs and exports: `/private/tmp/r62-final-cohort`,
  `/private/tmp/r62-{full-build-final,fixed,targeted,sweep,final-cohort}.log`.

No commits, pushes, stash operations, holdout evaluation, full test suite or RSS
benchmark. The phased/try groups remain outside this increment.
