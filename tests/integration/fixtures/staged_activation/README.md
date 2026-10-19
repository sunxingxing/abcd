# Activation-local staged literals

The pinned DevEco compiler identity and source/ABC hashes are in `manifest.json`.
Compile each of `loops.js` and `caught.js` from the repository root:

```
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 --output=tests/integration/fixtures/staged_activation/loops.abc tests/integration/fixtures/staged_activation/loops.js
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 --output=tests/integration/fixtures/staged_activation/caught.abc tests/integration/fixtures/staged_activation/caught.js
```

`staged_activation_source_test` exports `loops.abc` with the production engine,
checks the emitted module with Node, and compares it against the source oracle.
The tests cover conditional RHS effects, zero/multiple iterations, independent
loops before/after construction, nested fresh allocations, distinct object
identities, and exceptions during initialization (constructor type and traces).
No source file is an engine input. This is ordinary JS evidence, not Ark VM execution.

`caught.abc` is a conservative negative: a handler whose loop retains a cycle in the unfinished
allocation-dominated region remains a located
`kLiteralStagedAcyclic` refusal. No exceptional edge is removed to admit it.
The unit test additionally rejects re-entry before completion and re-execution
of definitions without reallocation; it exercises 200/201/400/800 fields and
exact byte/work budgets in construction and independent verification.
