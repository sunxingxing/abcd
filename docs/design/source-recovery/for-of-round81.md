# Round 81: mixed transparent forwarding and for-of control

Implementation on baseline `1c12589b`, without a commit. This change admits a
remaining ordinary wrapper only with independently checked transparent
forwarding evidence. It does not grant close ownership to ordinary code.

## Method and record boundaries

All six methods have verified transparent forwarding for the ordinary handler.
The full seal still requires every close obligation, including the pre-existing
whole-method nested/current-completion obligation.

| o32 record / method | Ordinary try body / handler | New standalone boundary | Production record boundary |
|---|---|---|---|
| event 23058 / m23563 | Try1 8 / 20 | Seal, control, identity, iteration, events and independent evaluation pass | Unchanged m23306: PhasedMethodError::kControl > ExceptionControlError::kMultipleExits, original body149/H174, working125/H150 |
| event 23058 / m23590 | Try1 8 / 20 | Same evaluation proof passes | Same m23306 refusal |
| DeepTypeUtils 38853 / m39263 | Try1 18 / 25 | Same evaluation proof passes | Now m39073 op85 PC209: FunctionTreeError::kControl > kDecisionPrefix > DecisionPrefixError::kPrefixArms; not complete |
| service 58223 / m60524 | Try1 204 / 216 | Same evaluation proof passes | Unchanged m58418: FunctionEmitError::kKind > AsyncCompletionCheck::kPreludeEffects (1), PC18 |
| service 58223 / m61486 | Try2 126 / 142 | Original forwarding refusal gone; CloseCompletionError::kNested, protocol0 first return read PC337 (second protocol read PC633) | Same m58418 refusal |
| service 58223 / m61706 | Try2 118 / 130 | Original forwarding refusal gone; CloseCompletionError::kNested, protocol0 first return read PC645 (second protocol read PC870) | Same m58418 refusal |

The record-level table is based on production manifests, not the standalone
probe's conditional entry/capture facts. `abc_semantic --function-source` cannot
recover these open member contexts by itself; its decoded/input-stage receipt is
not substituted for a production record's actual first refusal.

## ComponentFactory recompilation

The exact engine output (with identifier export aliases unquoted only at the
pinned compiler boundary) recompiles to m425. Ordinary Try1 body61/H88 is now
proved forwarded. The full ownership seal, control projection, and independent
for-of evaluation pass. The earlier seal refusal was a representation gap,
not a difference in iterator or close semantics. The fixture integration test
also recompiles newly emitted output and reruns the full protocol proof.

## Acceptance

Evidence lives in `/private/tmp/round81` (temporary, untracked).
Both exploration flag sets retain **526/620** and **573/620**. Compared with the
Round80 exports (byte-identical to `1c12589b`), there are zero status changes,
regressions or already-complete source/extension changes. Old flags: all files
identical. V4: only `o32/manifest.json` changes, updating DeepTypeUtils' first
refusal to the next blocker listed above. No new complete cohort record is
claimed. The new small fixture completes through the production engine.

Validation commands and actual results:

- `cmake --build build/regression -j 6 -- -k`: passed.
- New forwarding tests: 4/4 passed (proof, geometric scale/exact budgets,
  equivalent variants, production/Node/recompile integration).
- `ctest --test-dir build/regression -R 'for_of|protocol_topology|for_in|recovery_flow' -j 1 --output-on-failure`:
  31/31 passed, including the four new tests.
- `ctest --test-dir build/regression -R 'cli|corpus|failure|_receipt|manifest' -j 1 --output-on-failure`:
  76/77 passed. The only failure is the already registered
  `family_source_cli_test` (`node escaped its owner or origin arena`);
  no expectation was weakened or changed.
- Fresh final-binary cohort exports reproduce the same comparison above.
  Peak reported CLI RSS: old flags 39,436,288 bytes; V4 39,501,824 bytes.
- `git diff --check`: passed. No commit, push or stash was performed.
- Final-binary `ctest --preset source-regression -j 1 --output-on-failure`:
  **65/65 passed**, 170.48 seconds (a prior run also passed 65/65).
