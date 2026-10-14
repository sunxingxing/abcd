# Logical route coverage (Round 61)

Independent source oracle; the engine receives only `coverage.abc`.
DevEco Studio bundled es2abc, ABC 24.0.0.0:

```sh
compiler=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
"$compiler" --module --merge-abc --opt-level=0 --record-name=coverage --source-file=coverage.js --output tests/integration/fixtures/logical_coverage/coverage.abc tests/integration/fixtures/logical_coverage/coverage.js
```

SHA-256 `coverage.abc`:
`7c009b0ebb0f62f3d4db1e643c4c48636b40e8c440ee1fbe67e17f99be5bc000`.

The CLI test first checks the actual instructions: each method evaluates a
conditional before creating the buffered object and initializing its fields.
The stored selection must retain its original evaluation even when the
initializer already belongs to the literal plan. `argument` also distinguishes
the later callee getter and its receiver; `nested` has effects between the
selection and the field. The IR test directly recreates the orphan registration
and requires the independent verifier's ConsumedEvent refusal.

Both flag sets run 56 scenarios each, comparing values, exception constructors,
and full traces with the independent Node source oracle. They cover nested and
short-circuit conditions, getters throwing at each site, truthy objects whose
valueOf must not run, and a selected arm changing the later callee. Five mutants
per flag set change the predicate, duplicate a selected getter, change the
selected value, remove the stored result, or insert a later getter too early.
All ten must be detected. No finally-throw case is present: those require the
ABC handler-range oracle, not uncompiled JS. This is not pinned Ark execution.
