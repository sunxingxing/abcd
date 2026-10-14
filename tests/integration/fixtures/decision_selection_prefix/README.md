# Verified selection in a decision prefix (Round 62)

The independent source oracle is `prefix.js`. The engine sees only `prefix.abc`.
Compiler: DevEco Studio bundled es2abc (ABC 24.0.0.0).

```sh
compiler=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
"$compiler" --module --merge-abc --opt-level=0 --record-name=prefix --source-file=prefix.js --output tests/integration/fixtures/decision_selection_prefix/prefix.abc tests/integration/fixtures/decision_selection_prefix/prefix.js
```

SHA-256 of `prefix.abc`:
`0cb035b3d0c391d4b7c04563f961b5dab71b9db06f95c961ddf24c4f8e3d5425`.

`decision_selection_prefix_cli_test` first checks conditional branches and named
reads in all four compiled methods, then exports both contracts and compares
1344 executions against Node: OR/AND short-circuit, nested conditional values,
truthy objects (no valueOf), a conditional statement prefix, property-read order and counts, getter-induced
rebinding, throws at each read/call, receiver identity and later reads. Ten
mutants change the inner predicate, duplicate/remove a selected read, invert the
short-circuit connective or move a later read earlier; all are detected.
The raw-IR unit test separately asserts `DecisionPrefix::kRegion`, tests copy,
register and block-split equivalents, statement prefixes, forged continuation,
missing selection evidence, reordered arms, missing/duplicated event placements,
and exact byte/work boundaries at 200/201/400/800 units.

No finally-throw case is present. Those require ABC handler-range expectations
rather than the original JS oracle. These checks are Node source-level evidence,
not a claim of execution in the pinned Ark interpreter.
