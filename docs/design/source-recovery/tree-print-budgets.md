# TreePrint budget evidence (Round 47)

This round changes diagnostics only. `TreePrintError` values, resource classes,
record admission, budgets, output reservation, syntax planning and printing
remain unchanged. The record cap is still 128 MiB and the process contract is
still 1,000,000,000 bytes. No exploratory or holdout admission depends on names.

## Failure witness

`TreePrintFailure` is trivially copyable and owns no dynamically allocated data.
Its optional charge has a typed budget kind (scratch/output/work/capacity/host),
requested, remaining, limit, used, peak and retained. The lower checker records
its **own** failed charge. An outer call does not invent a request from its
remaining allowance. `TreeStructureError` remains in the cause chain.

The print site distinguishes structure checking, variable projection, class
names, positions, actions, class-member retention, token output, destination
checking, borrowed tree storage and publication. The method and stable family
owner are retained; expression/statement/anchor IDs and original/working PCs
are attached only when the actual action has that identity/origin. Bulk table
allocation does not have an instruction PC. PC 0 is not missing. Combined and
synthetic origins are not assigned an invented representative PC.

The bounded, synchronous diagnostic scope is thread-local and stack nested.
It never controls admission. Lower stages propagate a charge only on failure;
successful lower analyses discard their diagnostic scratch. Error unwinding
retains the failing site. There are no new graph indexes or heap buffers.
Serialization preserves the witness in route receipts, record-entry failures,
manifest records and `abc_semantic --record-source`.

`peak` and `retained` describe the particular ledger, **not allocator RSS**.
For the present conservative ledgers, `retained` is the active deducted amount;
that can include borrowed input or already-destroyed scratch. `written_bytes`
is confirmed completed output in the failing print invocation; an I/O short
write may have additionally accepted an unknown partial token. Missing host
allocation/overflow request means its byte request is unknown, not zero.

## Counting diagnostic

```
abc_semantic INPUT.abc ENTRY_METHOD --record-print-budget --language-source
abc_semantic INPUT.abc ENTRY_METHOD --record-print-budget --language-numeric --language-constructors
```

`CountingSink` executes the real writer's actions and retains no text. The CLI
reconstructs the same record and performs the count before allocating/reserving
its production output stream. The count uses only the already-remaining record
allowance; it does not increase the record or process cap. All diagnostic
positions are destroyed before the unchanged production attempt. A count is
not a source-completion result. If counting fails, `count_complete` is false and
`counted_bytes` is only a prefix. The output allowance remains the existing
64 MiB `EmitBudget`; no unlimited counting or production bypass is introduced.

The report includes production reservation/scratch, pre-reservation available
bytes, counted output, counted printer charge peak, and the visited methods,
expressions, statements, operation placements and anchors. Census follows the
same tree ownership traversals; it does not walk shared subtrees per query.
The additional work is one diagnostic-only printer invocation plus constant
work per visited tree; output storage is O(1).

## Round 48 direction (not implemented)

Let A be the allowance left after record preparation, R the present output
reservation, B the measured UTF-8 output, and P the unchanged printer's charge
peak. Today R=min(output_limit,floor(A/6)), then 5R is deducted. This reserves
for the *permitted maximum*, even for a tiny actual output. Compare P with A-5R,
and compare P+5B with A. This arithmetic is evidence for a reservation problem,
not permission to raise either cap.

Independently, `VerifyTreeStructure` charges `MeasureTreeStorage(tree)` although
its input is borrowed from the already-retained record. The printer then
charges `TreeStructure.allocated_bytes` as retained, although most checking
scratch has died; family loops repeat that pattern. `FamilyEnclosure` similarly
returns only counts and cost, but its destroyed checker's whole charge is
subtracted by `PreparedFamily::PrintDraft`. Correcting this requires explicit
borrowed/live/peak accounting and lifetimes, not removing validation.

A follow-up should first separate those ledgers, then plan exact bounded output
storage or a bounded spool using the real token writer. Reuse checked plans or
charge the extra counting work explicitly; never reset the work budget to hide
a second pass. Reserve only the proven live peak, release only dead owned
storage, and retain independent verification. Preserve all existing output
text and typed fatal I/O/allocation behavior. Raising 128 MiB / 1 GB is not
proposed.

The 200/201/400/800 unit series checks the real printer's counted bytes and
charged work/peak. It is a controlled growth check, not proof that every
recovery analysis has a linear complexity bound. The record measurements below
must not be interpreted as evidence that a super-linear algorithm is impossible.

## Round 47 measured records

Baseline: `5f1f71de`, exploration cohort only. Both flag sets were inspected.
Previous flags = `--language-numeric --language-constructors`; S =
`--language-source`. All amounts below are bytes. The lower charge's limit is
its own allowance, not the record's 128 MiB cap.

There are **7 printing refusals under previous flags, 10 under S** in this
requested set. For o19/22760, o19/35025 and o34/10794, previous flags stop before
printing at `CaptureCellError::kInput > CaptureError::kScope >
ScopeError::kInitialization`. No count/output requirement was measured for those
three previous-flags cases. The other seven have identical measurements in both
flag sets; “both” below denotes that verified equality.

| Previous-flags pre-print refusal | Child method | Original / working PC | Capture occurrence |
|---|---:|---|---|
| o19/22760 | 25401 | 112 / 88 | ordinal 1, depth 0, slot 0 |
| o19/35025 | 37543 | 112 / 88 | ordinal 1, depth 0, slot 1 |
| o34/10794 | 13533 | 112 / 88 | ordinal 1, depth 0, slot 1 |

Every printing refusal below is `kScratchBytes` / `TreePrintError::kResourceLimit`.
None is an output, work, capacity or host-allocation failure. All are bulk
analysis/table charges without an instruction origin, so no PC is invented.
Peak/retained is the **failing lower ledger**, not total printer memory.

| Record | Flags | Site; method / owner | Requested | Remaining | Limit | Peak / retained |
|---|---|---|---:|---:|---:|---:|
| o4/7635 | both | variable projection; 7758 / 0 | 17,866,872 | 17,608,053 | 17,695,883 | 87,830 / 87,830 |
| o5/13697 | both | borrowed tree storage; 15875 / 91 | 160,992 | 97,164 | 99,696 | 2,532 / 2,532 |
| o17/4729 | both | borrowed tree storage; 6055 / 97 | 113,136 | 36,651 | 40,327 | 3,676 / 3,676 |
| o19/22760 | S only | structure verification; 23974 / 67 | 552 | 78 | 34,468 | 34,390 / 34,390 |
| o19/35025 | S only | position table; 37124 / 109 | 240 | 87 | 3,060,622 | 3,060,535 / 3,060,535 |
| o23/4775 | both | structure verification; 5536 / 79 | 64 | 2 | 45,724 | 45,722 / 45,722 |
| o30/9674 | both | borrowed tree storage; 10797 / 98 | 1,440 | 505 | 26,229 | 25,724 / 25,724 |
| o30/18644 | both | borrowed tree storage; 21281 / 109 | 35,520 | 30,269 | 31,629 | 1,360 / 1,360 |
| o34/10794 | S only | borrowed tree storage; 12493 / 93 | 3,280 | 3,080 | 59,912 | 56,832 / 56,832 |
| o36/22557 | both | borrowed tree storage; 23006 / 87 | 49,152 | 14,719 | 16,331 | 1,612 / 1,612 |

M = visited method trees (also equal to the record metadata method count in
these ten records); O = operation placements; E/S = expressions/statements.
B includes imports, body and exports, using the real token writer. P is the
unchanged counting invocation's conservative printer charge peak.

| Record | M / O / E / S | B: counted output | R: reservation | A−5R: production scratch | P: counted charge peak | P+5B | A: available |
|---|---|---:|---:|---:|---:|---:|---:|
| o4/7635 | 2 / 214 / 1049 / 34 | 10,708 | 19,246,138 | 19,246,140 | 19,743,049 | 19,796,589 | 115,476,830 |
| o5/13697 | 93 / 2204 / 2448 / 643 | 31,228 | 14,526,269 | 14,526,270 | 14,791,044 | 14,947,184 | 87,157,615 |
| o17/4729 | 103 / 3225 / 2745 / 862 | 30,812 | 13,435,307 | 13,435,309 | 17,066,443 | 17,220,503 | 80,611,844 |
| o19/22760 | 117 / 3521 / 3064 / 1096 | 32,480 | 13,300,184 | 13,300,187 | 15,049,380 | 15,211,780 | 79,801,107 |
| o19/35025 | 112 / 3339 / 2738 / 1007 | 28,488 | 13,727,840 | 13,727,845 | 13,900,616 | 14,043,056 | 82,367,045 |
| o23/4775 | 105 / 3368 / 2883 / 875 | 31,575 | 13,156,886 | 13,156,889 | 17,942,376 | 18,100,251 | 78,941,319 |
| o30/9674 | 144 / 2935 / 3342 / 1151 | 40,091 | 12,824,448 | 12,824,450 | 19,024,952 | 19,225,407 | 76,946,690 |
| o30/18644 | 140 / 2748 / 3091 / 1067 | 36,505 | 13,127,071 | 13,127,074 | 17,967,339 | 18,149,864 | 78,762,429 |
| o34/10794 | 122 / 3540 / 2845 / 982 | 30,651 | 13,394,988 | 13,394,990 | 14,509,784 | 14,663,039 | 80,369,930 |
| o36/22557 | 120 / 2830 / 2233 / 728 | 28,467 | 13,619,898 | 13,619,898 | 15,054,376 | 15,196,711 | 81,719,388 |

### Classification and arithmetic

Each of the ten records has the same two evidenced accounting problems:

- **A: output reservation is sized against an unused maximum.** R is 320–1,797
  times the actually counted B. Every row satisfies `P > A−5R`, but `P+5B < A`.
  Example o4: 19,743,049 > 19,246,140; yet 19,743,049 + 5×10,708 =
  19,796,589 < 115,476,830. Example o30/9674: 19,024,952 > 12,824,450;
  19,024,952 + 5×40,091 = 19,225,407 < 76,946,690. Five buffers proportional to
  the **actual output length** already fit under the existing allowance, even
  with the present inflated P. These are measured exact counts, not a proposed
  arbitrary higher limit or a guessed bytes-per-op constant.
- **B: borrowed input / destroyed verification scratch is retained in the
  printer ledger.** `record_tree.cc` retains the built tree/family; then
  `tree_structure.cc::VerifyTreeStructure` charges `MeasureTreeStorage` again.
  `tree_printer.cc::Draft` charges each structure check's entire peak and does
  not release the checker's dead scratch. `PreparedFamily::PrintDraft` likewise
  subtracts `FamilyEnclosure.allocated_bytes` after that checker has died.
  The `kTreeStorage` rows catch this directly; the variable-projection and
  position-table rows reach their later charge after those earlier deductions.
  This does not mean existing borrowed `ClassNames` / `SourceVariables` plans
  should be charged again: their current borrow/release handling is separate.

No row supplies evidence of a super-linear *output* blowup: all ten counts
complete inside the unchanged remaining record cap, and their outputs are only
10,708–40,091 bytes. This is not proof that every binding-recovery subalgorithm
is linear. Do not raise limits on that inference. Round 48 should repair A/B
with explicit lifetime accounting and a proven output-storage bound; any
remaining super-linear stage needs its own growth evidence and causal fix.

Controlled real-printer series (debugger statements; not corpus tuning):

| N | Output bytes | Charged peak | Work |
|---:|---:|---:|---:|
| 200 | 2,417 | 317,426 | 43,289 |
| 201 | 2,429 | 318,161 | 43,502 |
| 400 | 4,817 | 632,362 | 85,889 |
| 800 | 9,617 | 1,262,234 | 171,089 |

Output is exactly `12N+17`, work `213N+689` in this series. Charged peak tracks
geometric vector capacity; doubling 200→400→800 is approximately ×1.99→×2.00.
The unit test also checks the exact admitted scratch boundary, one byte below
it, output length equality, and output length minus one.

## Validation scope

New tests are `tree_print_failure_test` and `tree_print_budget_cli_test`.
The unit covers every budget-kind/site serialization pair, missing and zero
PCs, original/working PC distinction, unchanged resource classes, a real
released/retained lower ledger, work/capacity/host-allocation failures, and
counted versus real output. The CLI test compares a real fixture's source byte
lengths and checks the same resource witness in the record manifest,
`--record-source`, and `--record-print-budget`.

The 12-test directed group also includes `source_route_test`,
`tree_failure_test`, `record_failure_json_test`, `partial_tree_test`,
`tree_structure_test`, `module_family_print_test`, `class_tree_context_test`,
`class_accessor_tree_test`, `module_default_export_test`, and
`tree_failure_cli_test`. It passes **11/12**; both new tests pass.

`tree_structure_test::BlockExits` still asserts that a side-effect-free body
prints an `$exit0` label and a break (line 356), while the current recovery
prints an empty function. Its assertion is unchanged. A controlled comparison
using HEAD's old `tree_printer.cc`, adapted only to the new failure result type
and linked with the same remaining libraries, fails at the same assertion with
the same output. This is not a full baseline checkout/build and is not claimed
as one of the previously recorded 30 failures. No assertion was weakened.

Final workspace verification (2026-10-14, uncommitted):

| Check | Result | Local evidence |
|---|---|---|
| `cmake --build build/regression -j 6 -- -k` | exit 0 | `/private/tmp/r47-final-build.log` |
| `ctest --preset source-regression -j 1 --output-on-failure` | 65/65 | `/private/tmp/r47-final-fixed.log` |
| Directed group above, `-j 1` | 11/12; new tests 2/2 | `/private/tmp/r47-final-targeted.log` |
| Previous flags versus `explore46` | 488/620; 0 status changes, 0 text changes | `/private/tmp/r47-cohort-verified.log` |
| Source flags versus `explore46s` | 522/620; 0 status changes, 0 text changes | same log |

The cohort comparison checks the record sets, every status, and every published
source file byte-for-byte (including published incomplete sources). New
diagnostic JSON fields are intentionally different. Exports are under
`/private/tmp/r47-cohort-verified/`; the 20 per-record diagnostic reports are in
`/private/tmp/r47-targets/rows.json`. No holdout, full test suite or RSS benchmark
was run. The peaks above must not be presented as measured process RSS.
