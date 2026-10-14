# Round 48: TreePrint ledger verification (uncommitted)

Baseline `057aa5cb`, 2026-10-14. Implementation and text/status acceptance are
complete, with one unresolved hard acceptance condition: o4/7635 materially
increases production peak RSS because it now reaches a previously refused
allocation. This is reported for cc review, not waived. No cap has been raised;
no flow-solver optimization is included. No commit, push or stash was made.

Design and lifetime proof: [tree-print-budgets.md](tree-print-budgets.md#round-48-lifetime-ledger-and-exact-output-storage-workspace-review).

## Changes

- `print_memory.h`, `borrowed_print_trees.h`: pre-allocation leases, tracked live
  storage, exact tree/held-owner borrowing index and conservative fallback.
- `tree_structure.{h,cc}`, `family_enclosure.{h,cc}`, `prepared_family.cc`,
  `tree_printer.cc`: separate input, retained scopes and destroyed scratch;
  preserve peak evidence; return credit only after the storage owner dies.
- `record_tree.cc`, `record_print_sink.h`, `source_text.h`, `printed_source.h`:
  one shared counting/emitting action sequence; exact B-byte output allocation
  charged before construction and moved into the result without a second string.
  Keep cumulative work and configured output limit. `counting_sink.h` now
  includes its own fixed-width integer dependency.
- `value_source_inspect.cc` and three test callers use `SourceText::View()` at
  textual boundaries. `tree_print_budget.mjs` now requires o4 to complete,
  compares its manifest source with `--record-source`, and requires counted
  length = emitted length = output reservation.
- CMake manually registers `print_memory_test`. No print-action or formatting
  branch was changed.

## Tests and commands

| Command / group | Actual result | Evidence |
|---|---|---|
| `cmake --build build/regression -j 6 -- -k` | exit 0 | `/private/tmp/r48-verified-build.log` |
| `ctest --preset source-regression -j 1 --output-on-failure` | 65/65 | `/private/tmp/r48-fixed.log` |
| Directed group below, `-j 1 --output-on-failure` | 17/18 | `/private/tmp/r48-targeted.log` |
| New ledger + existing print-failure/CLI tests | 3/3 (included above) | same log |
| Previous flags versus `explore47` | 495/620, +7; 0 regressions, 0 text drift | `/private/tmp/r48-cohort-final.log` |
| `--language-source` versus `explore47s` | 532/620, +10; 0 regressions, 0 text drift | same log |

The cohort comparison checks the record sets, all statuses, and every previously
published source file byte-for-byte, including incomplete source. Exports are in
`/private/tmp/r48-cohort-final/explore48` and `explore48s`. No holdout, full test
suite or full-corpus RSS benchmark was run.

Directed tests (all passed except the explicitly marked pre-existing failure):

- `print_memory_test` — passed.
- `tree_print_failure_test` — passed.
- `class_tree_context_test` — passed.
- `binding_initializer_test` — passed.
- `module_family_print_test` — passed.
- `tree_structure_test` — pre-existing abort; cc verified since `22e7423a`; assertion unchanged.
- `family_enclosure_test` — passed.
- `partial_tree_test` — passed.
- `class_accessor_tree_test` — passed.
- `class_names_test` — passed.
- `module_default_export_test` — passed.
- `record_import_tree_test` — passed.
- `record_tree_test` — passed.
- `source_route_test` — passed.
- `record_function_test` — passed.
- `record_failure_json_test` — passed.
- `tree_print_budget_cli_test` — passed.
- `tree_failure_cli_test` — passed.

The new allocation override test compares actual live allocation payloads with
ledger live bytes. It detects omitted admission, rejects early release, keeps
returned expression scopes charged, checks borrowed/owned/expired-owner cases,
and checks old+new growth and exact output admission at 200/201/400/800. It also
checks real/counting equality and one-less output/work refusals. The output
strategy allocates exactly once; there is no production geometric-growth path.

| N | Counted bytes | Direct printer scratch | Direct printer work |
|---:|---:|---:|---:|
| 200 | 2,417 | 306,675 | 43,288 |
| 201 | 2,429 | 307,360 | 43,501 |
| 400 | 4,817 | 611,611 | 85,888 |
| 800 | 9,617 | 1,221,483 | 171,088 |

These controlled debugger-statement fixtures have B = 12N+17 and work =
213N+688. They do not establish a bound for every existing flow-analysis input.

## Ten targets and production RSS

Each number below is `peak_rss_bytes` from a fresh CLI process using
`abc_semantic <abc> <entry> --record-source`, before and after. The baseline was
measured before replacing the binary; its binary was also saved for controlled
repeats. On Darwin the report correctly says no established hard address-space
limit. Measured RSS is distinct from ledger peak and remains below 1 GB.

| Record | Previous flags result | Source flags result | Previous RSS before → after (bytes) | Source RSS before → after (bytes) | Source output B |
|---|---|---|---:|---:|---:|
| o4/7635 | complete | complete | 12,206,080 → 30,998,528 | 12,140,544 → 31,014,912 | 10,708 |
| o5/13697 | complete | complete | 22,200,320 → 22,396,928 | 22,282,240 → 22,298,624 | 31,228 |
| o17/4729 | complete | complete | 26,411,008 → 26,574,848 | 26,411,008 → 27,607,040 | 30,812 |
| o19/22760 | capture initialization (below) | complete | 10,125,312 → 10,190,848 | 27,885,568 → 27,836,416 | 32,480 |
| o19/35025 | capture initialization (below) | complete | 10,338,304 → 10,059,776 | 27,033,600 → 27,066,368 | 28,488 |
| o23/4775 | complete | complete | 27,377,664 → 27,295,744 | 26,869,760 → 27,328,512 | 31,575 |
| o30/9674 | complete | complete | 27,115,520 → 26,820,608 | 27,131,904 → 26,738,688 | 40,091 |
| o30/18644 | complete | complete | 26,656,768 → 26,132,480 | 26,230,784 → 26,312,704 | 36,505 |
| o34/10794 | capture initialization (below) | complete | 9,912,320 → 9,879,552 | 27,328,512 → 27,344,896 | 30,651 |
| o36/22557 | complete | complete | 25,198,592 → 24,723,456 | 24,805,376 → 24,854,528 | 28,467 |

The three unchanged previous-flags refusals remain
`CaptureCellError=3 > CaptureError=1 > ScopeError=3` (requested Language):

| Record | Method | Occurrence / cell | Original / working PC |
|---|---:|---|---|
| o19/22760 | 25401 | ordinal 1, depth 0, slot 0 | 112 / 88 |
| o19/35025 | 37543 | ordinal 1, depth 0, slot 1 | 112 / 88 |
| o34/10794 | 13533 | ordinal 1, depth 0, slot 1 | 112 / 88 |

Raw reports: `/private/tmp/r48-before/rows.json`,
`/private/tmp/r48-after/rows.json`. The source result for every target is complete;
there is no new typed refusal to report for that flag set.

### RSS exception: o4/7635

The old production run refuses at variable projection with a genuine lower
charge request of **17,866,872 bytes**, while only **17,608,053** remain in that
lower budget. It has not allocated the requested dependency vector yet. The new
ledger allows the existing analysis to run, and its production RSS increases
from about 12 MB to 31 MB. This is material; the requested production RSS gate
is **not met** for this record.

A temporary, separately linked allocation-observer executable (no production
source edits) identifies `SolveFlowNormalKinds` at `flow_kinds.cc:373`,
`budget.Array(dependencies, edges)`, through `PlanBindingInitializers` and
`PrepareSourceVariables`. Each counting/emitting pass reaches the allocation;
the allocations have disjoint lifetimes. The baseline counting diagnostic also
reaches it and uses about 31 MB RSS. This distinguishes newly reachable existing
analysis from simultaneous duplicate output buffers, but does not excuse the
production-before/after increase or prove the solver's general scaling.

Allocation evidence: `/private/tmp/r48-alloc-probe.log`; old diagnostic reports:
`/private/tmp/r48-before-count/rows.json`; alternating fresh-process repeats:
`/private/tmp/r48-rss-repeat.json`. A follow-up decision is required before
claiming the full hard acceptance. Do not silently substitute the diagnostic RSS
baseline for the old production baseline, add sample admission, or raise a cap.

### Alternating RSS repeats

Each cell below is the median of three fresh processes, alternating baseline
and current; o4 also includes the baseline counting diagnostic. These are
supplementary repeats, not replacements for the production table above.

| Record / flags | Baseline production | Current production | Baseline counting diagnostic |
|---|---:|---:|---:|
| o4/7635 previous | 12,337,152 | 31,047,680 | 30,916,608 |
| o4/7635 source | 12,238,848 | 30,998,528 | 30,949,376 |
| o17/4729 previous | 26,443,776 | 26,509,312 | not run |
| o17/4729 source | 26,443,776 | 26,509,312 | not run |

The single o17 source measurement's +4.5% increase did not recur consistently;
its repeated median difference is +0.25%. The o4 increase is stable.

## Review focus

Review the still-held tree-owner witness and nested-family input lifetime;
returned expression-scope destruction before credit; both passes' cumulative
work; exact output ownership transfer and the public move-only `SourceText`
boundary. The debug invariant covers allocation payloads on controlled fixtures,
not allocator metadata or every engine allocation. Existing conservative lower
ledgers remain conservative. The production RSS exception above is unresolved.
