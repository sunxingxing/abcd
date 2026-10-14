# Retained phased selection in a decision prefix (Round 63)

`prefix.js` is the independent source oracle; the engine reads only `prefix.abc`.
Compiler: DevEco Studio bundled es2abc, ABC version 24.0.0.0.

```sh
compiler=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
"$compiler" --module --merge-abc --opt-level=0 --record-name=phasedprefix --source-file=prefix.js --output tests/integration/fixtures/phased_decision_prefix/prefix.abc tests/integration/fixtures/phased_decision_prefix/prefix.js
```

SHA-256:
- `prefix.js`: `daa22c118c2a97d3735012a60d63cddbc67d6c15a4aa89760e54b03f133648d7`
- `prefix.abc`: `fda546a67852d161e9ce344a3d7e2780b54de110cd705d2685dcaa02e66e8204`

The integration test first asserts branch instructions, property reads and actual
handler dispatch in both compiled methods. Under both contracts it exports native
try/catch source and compares results, exception constructors and complete traces:
short circuit, true/false materialization, conditional getters, later reads,
getter-induced rebinding, receiver identity, truthy objects without valueOf, and
throws from predicates/arms/later reads/body calls. Mutants invert a predicate,
duplicate a getter, substitute an arm, invert the short circuit and move a later
read earlier. The raw-IR test separately checks retained root placement, copy,
register and block-split equivalents, swapped arms, moved/removed/duplicated root
events and a wrong exception edge. Scale tests use 200/201/400/800 selected prefixes
and exact byte/work boundaries under the existing 128 MiB record allowance.

No finally-throw scenario is present. Such scenarios require ABC handler-range
expectations rather than the original JS oracle. Node execution is source-level
differential evidence, not execution on the pinned Ark VM.
