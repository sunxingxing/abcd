# Round 82: phased selection binding through protocol control

Baseline: `31687603`. The evidence is from original ABC and the production
pipeline; paired source does not drive discovery or admission.

## Direct cause

DeepTypeUtils o32/38853, m39073 reads each key in a verified for-of. Its body
contains the equivalent of:

```js
if (typeof key === "symbol" || !new RegExp("^[0-9]+$").test(key)) {
  output.push(key);
}
```

The original relevant instructions are:

| PC | Meaning |
|---|---|
| 152, 159, 162, 165 | typeof, strict equality with string pool1132 (`symbol`), truth test, short-circuit jump to PC220 |
| 167–198 | RegExp global read, construction with pool1134 (`^[0-9]+$`), member read/call `test(key)` |
| 206 | callruntime.istrue of the call result |
| 209 | op85: conditional jump to PC214 |
| 211–214 | true / jump to PC215 / false |
| 215, 218 | isfalse on the merged Boolean, conditional jump to PC244 |
| 220–238 | output.push(key) |

The call is ordered and possibly throwing (original body handler H246). The
op85 selection itself is a retained `EvaluationKind::kRoot` with pure Boolean
arms, not a protocol event. `FindPhasedPhiSelections` already proves header171,
join178 and its original phi inputs. The original op lives at Before170; the
branch executes at Commit171. The compressed control node is165, its `.block`
is165, and its next node172 maps to original join178. The old phased adapter
used `.block` directly as a WorkBlockId, discarded the candidate and left the
round-63 decision emitter with an unrecognized prefix having arms.

The initial standalone evidence listed candidates branch20 (header41/join48)
and branch85 (header171/join178), but only branch20 survived control binding.
With the mapping corrected, the production draft, independent TreeFlow,
OriginalFlow and recovery pass for m39073. No new evaluation admission or event
protocol is needed.

## Change and verification boundary

`FindSelectionExpressions` / `FindPhasedSelections` accept the verified
ExceptionControl mapping. Header, arm, join and condition-path binding resolve
PhaseControlUnit identities; try wrappers and ProtocolCloseUnit cannot become
selection phases. The builder and TreeFlow independently rerun the same
original-edge binding proof after control verification. The retained selection
is consumed by the existing round-63 path, including its phi assignment,
conditional placement, catch scope, staging and single-consumption checks.

`ProjectSelectionLogicals` checks phase-path effects through the original
`condition_phases`, rather than reinterpreting control-unit IDs as block IDs.
The normal route is unchanged. The mapping adds constant work per existing
control visit and no allocated index. Existing prepaid buffers and work bounds
remain in force; there is no new per-region scan or source-language premise.

Tests cover a real es2abc `left(value) || !right(value)` in a for-of and the same
expression outside it. The production-draft test mutates the phase mapping,
drops/duplicates selection ownership and reverses conditional arms, then
requires independent control/tree/recovery rejection. Register and split-block
variants and 200/201/400/800 graph scales exercise exact resource boundaries.
The ordinary phased-prefix test compares old and explicit mapping results,
including 200/201/400/800 retained selections. Node checks emitted engine code,
short-circuit call order, throws/close, and ToBoolean without invoking valueOf.

## Record result

The original m39073 op85 PC209 refusal is gone. DeepTypeUtils is **not complete**:
its next production first refusal is in m39263:

`FunctionTreeError::kSemantics (9) > EvaluationError::kOwnership (5) >
EvaluationObligation::kPlan (0) > TreeObligation::kSemantics (14)`.

The existing witness gives method39263 with no OpId or PC; none is invented.
This next obligation is outside the decision-prefix mapping fix.

## Acceptance

Temporary evidence: `/private/tmp/round82`. Baseline cohorts were exported from
the unchanged production binary at `31687603` before the fix. Both after-runs
retain **526/620** (previous flags) and **573/620** (V4): zero regressions and zero
already-complete text/extension drift. Previous flags are byte-identical in all
files. V4 changes only o32/manifest.json, replacing DeepTypeUtils' first refusal
with the m39263 cause above.

Validation:

- `cmake --build build/regression -j 6 -- -k`: passed.
- Four new `for_of_selection*` tests: 4/4 passed.
- `ctest --test-dir build/regression -R 'for_of|phased_decision_prefix|decision_prefix_test|protocol_topology|recovery_flow' -j 1 --output-on-failure`: 27/27 passed. This includes the unchanged normal decision-prefix test, the ordinary phased-prefix mapping-equivalence tests and CLI differential, plus the for-of and recovery suite.
- Engine-output Node syntax/differential on the new fixture: passed. The independently verified m39073 draft also passes `node --check`; this does not imply record completion.
- `abc_semantic INPUT 39073 --record-source --language-source` reports the same m39263 cause as the record manifest.
- `git diff --check`: passed. No commit, push or stash.
- `ctest --test-dir build/regression -R 'cli|corpus|failure|_receipt|manifest' -j 1 --output-on-failure`: 77/78 passed; only the registered pre-existing `family_source_cli_test` failed (`node escaped its owner or origin arena`). No expectations needed updating.
- `ctest --preset source-regression -j 1 --output-on-failure`: **65/65 passed**, 169.18 seconds.
