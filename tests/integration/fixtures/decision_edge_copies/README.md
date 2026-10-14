# Decision-edge copies (Round 60)

Independent source oracle; the production engine receives only `edges.abc`.
Compiler: DevEco Studio bundled es2abc, ABC 24.0.0.0.

```sh
compiler=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
"$compiler" --module --merge-abc --opt-level=0 --record-name=edges --source-file=edges.js --output tests/integration/fixtures/decision_edge_copies/edges.abc tests/integration/fixtures/decision_edge_copies/edges.js
```

SHA-256 `edges.abc`:
`8d5e679e6330976222c99a37cc29fe66c9c9e3c33f7621e471d28aaf43a5aebf`.

The CLI test asserts named property reads and conditional branches in every
method, and an actual exception handler on the reads in `handled`. It also
asserts at least three edge-qualified conditional copy expressions in engine
output, so this is not merely an already-supported nested-if fixture.

Under both flag sets it compares `check`, `andCheck`, `handled`, and `swap` with
Node execution of the oracle: first/second/neither short-circuit paths, truthy
objects whose valueOf must not run, exceptions at each getter, two-value swaps
and later observations across three iterations. The 96 executions compare
returned values, exception constructors and complete traces. Six source mutants
remove/corrupt the copies or change the short-circuit connective; all must be
detected. IR mutations additionally check duplicate, wrong-edge and hoisted
copies, whose private writes need not themselves be externally observable.

No finally-throw scenario is present. Such a scenario must use the ABC handler
ranges as oracle, because es2abc inlined finalizers can differ from uncompiled
JS. These Node results do not claim execution in the pinned Ark interpreter.
