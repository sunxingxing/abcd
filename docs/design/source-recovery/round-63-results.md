# Round 63: omitted-member evidence and retained phased decision prefixes

Baseline: `c1e09c6b`, frozen `explore62` / `explore62s`.
No source premise, evaluation fusion, declaration, follow/join or catch-staging
policy was added. Temporary diagnostic instrumentation has been removed.
Implementation and independent verification:
[loop-discovery.md](loop-discovery.md#retained-phased-selection-prefixes-round-63).

## A — partial classes

The following evidence is identical under both contracts. Both records stay
`source_status=4`, and their incomplete source bytes remain unchanged.
`source_omitted_members` is shared by manifest and `abc_semantic --record-source`.
Only actual failure sites are published; a descendant's creation is separate.

| Record | Class constructor | Omitted member | Actual leaf method | Original / working PC | First typed refusal | Descendant creation |
|---|---:|---|---:|---|---|---|
| o8/10284 KeyboardController | 12788 | registerListener, m12733 | 11580 | 49 / 49 | SemanticProofError::kInternalValue (3) | m12733 @224 |
| o20/10622 CustomAnimationTab | 11284 | aboutToAppear, m11067 | 11067 | 633 / 633 | SemanticProofError::kInternalValue (3) | absent |

m11969 and m12066 are the earlier Round-62 decision-prefix blockers, not the
methods responsible for these omissions. Class/member preparation wrappers on
these paths are variants without an additional enum cause; the published leaf
is the actual retained SemanticProofError, not a fabricated class error.
Phased omissions retain ExceptionInputError and its exception-node/value IDs;
working PC is captured before original-PC mapping.

## B — phased root selections

| Record | Method / op | Original / working PC | Selection phase header -> join | Result, both contracts |
|---|---|---|---|---|
| o6/5058 AudioStreamController | m5417 / Op21 | 50 / 47 | 43 -> 50 | complete |
| o18/28264 TcpClient | m29676 / Op47 | 114 / 111 | 95 -> 102 | complete |
| o32/49116 coverageCollect | m49302 / Op141 | 367 / 362 | 283 -> 290 | complete |

These are ValueBranch Commit nodes whose selected value stays assigned to an
existing phi temporary. Their operation locations belong to the corresponding
Before blocks (42, 94, 282); these are not interchangeable with the selection
header/Commit identities. The source uses the retained assignment as a comma
prefix. It does not erase truth events, change the consumer or manufacture an
inline projection. Ordinary earlier/later getter/call events keep their existing
placement and exception handling.

## Evidence

- Full build: `cmake --build build/regression -j 6 -- -k` passed.
- Fixed set: `ctest --preset source-regression -j 1`: **65/65**.
- New tests: **4/4** (`phased_decision_prefix_test`,
  `phased_decision_prefix_cli_test`, `class_omissions_test`,
  `class_omissions_cli_test`), included in the targeted run below.
- Serial targeted regex
  `decision|logical|selection|evaluation|tree_failure|recovery|original_flow|class_omissions`:
  **63/65**. Only registered pre-existing `phased_arm_selection_source_test` and
  `decision_snapshots_source_test` failed; their assertions are unchanged.
- Serial sweep `cli|corpus|failure|_receipt|manifest`: **53/54**. Only the
  registered pre-existing `family_source_cli_test` failed, at the existing
  `family_source_cli.cmake:77` owner/origin-arena assertion. No new failure.
- Both final cohorts: **509/620** and **547/620**, each +3; **zero status
  regressions, zero byte drift**, including both partial-class texts. Only the
  five records in these tables change diagnostics. All six newly complete
  files pass `node --check`.
- The stale `tree_failure_cli_test` refusal for o18/28264 is replaced with a
  strict complete/source-equality check, retaining coverage of both output paths.
- `class_omissions_test` covers enum leaves, real PC/ID zero, absent working PC,
  normalized PC, descendant creation context and JSON round-trip/consistency.
  The real CLI test checks both partial records under both flags.
- `phased_decision_prefix_test` uses the production phased draft builder and
  independent TreeFlow/recovery; it covers copy/register/block-split equivalents,
  reordered arms, moved/dropped/duplicated root events, wrong exception edges,
  and 200/201/400/800 scale with exact byte/work budgets. The scale fixture
  counts the actual retained Root/ExprId branch placements.
- The real es2abc fixture verifies property reads, branches and handler dispatch
  before comparing exported source against Node (ordinary and throwing paths).
  Node checks exception constructors and traces, not messages or stacks. No
  finally-throw case is used; such cases require the ABC handler-range oracle.

Logs/exports: `/private/tmp/r63-build-final.log`, `/private/tmp/r63-fixed-final.log`,
`/private/tmp/r63-targeted-final.log`, `/private/tmp/r63-sweep-final.log`,
`/private/tmp/r63-verified-cohort.log`, `/private/tmp/r63-verified-cohort/`.

Exact scale boundaries (no production cap change):

| Selected prefixes | Bytes | Work |
|---:|---:|---:|
| 200 | 30,855,991 | 8,654,206 |
| 201 | 31,009,097 | 8,697,658 |
| 400 | 61,673,799 | 17,315,334 |
| 800 | 123,309,419 | 34,715,989 |

The Node fixture checks 576 differential executions and detects ten mutants
(five mutations under each contract). Actual Ark runtime execution remains
outside this evidence. Original unsupported Try-unit prefixes and nonempty
catch staging are not broadened by this increment.
