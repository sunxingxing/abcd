# Snapshot candidate consistency (Round 59)

Independent module source oracle; production receives only `candidates.abc`.
Compiler: DevEco Studio bundled es2abc, ABC version `24.0.0.0`.

```sh
compiler=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
"$compiler" --module --merge-abc --opt-level=0 --record-name=candidates --source-file=candidates.js --output tests/integration/fixtures/snapshot_candidates/candidates.abc tests/integration/fixtures/snapshot_candidates/candidates.js
```

SHA-256 `candidates.abc`:
`3a1b94b79d06210e4d936832c43c37b290e9cc8a8dec8d9c0b7b55a272ac4952`.

The CLI test asserts one LDOBJBYVALUE and two LDEXTERNALMODULEVAR instructions
per tested method, plus a real conditional edge. In the diagnostic-only build,
`check` (m397) refused at op12/PC29: its Use21 consumer op21/PC52 changed
from Root to LogicalLeft, producing SnapshotCandidateReason::kConsumerPlacement.
The final plan retains the indexed result at its original definition. The
`andCheck` case retains its previously valid saving assignment.

The test supplies `values.js` as an ordinary external module with a mutable
live export. It compares engine output with the source oracle under both
contracts: 36 executions covering first/second/no match, module rebinding,
list/index/A/B/new-B getter exceptions, one indexed read, and short circuit.
Exception constructors and complete traces are compared; message/stack are not.
Six mutants duplicate the indexed read, merge A/B reads, or invert the logical
connective. No finally-throw case is present; such a case would require an
ABC-handler-derived oracle rather than the uncompiled source expectation.
Node evidence does not claim execution in the pinned Ark interpreter.
