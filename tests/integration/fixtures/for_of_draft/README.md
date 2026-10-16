# Unpublished native for-of draft fixtures (II-b)

These sources are independent test oracles. The engine/draft builder reads only
ABC. No publication receipt is made: production must still return
`TreeObligation::kForOfRecovery`. Rendering a draft through the direct AST
printer is confined to the test harness.

Compiler: DevEco dynamic es2abc at
`/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc`,
SHA256 `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

From repository root, for each of `main` and `exits`:

```
es2abc --module --merge-abc --opt-level=0 \
  --output=tests/integration/fixtures/for_of_draft/main.abc \
  tests/integration/fixtures/for_of_draft/main.js
```

`main.abc`, method 387, contains acquisition, cached next, object check,
done/value, and compiler-expanded close handlers. It has a non-captured key,
an ordinary `last` phi/live-out (17 on zero iterations), a body call that may
throw, and a normal backedge. The production draft and independent TreeFlow
are tested; Node compares values, iterator/getter/call order and exception
constructors on ordinary iterables, including throw-triggered closing. This is
not original-flow/recovery acceptance, and not evidence for arbitrary Ark/native
iteration behavior. The V2 acquisition and V4 external-exception premises remain
required; a Node value cannot represent Ark's internal Hole.

`exits.abc`, method 408, exercises explicit break/return/continue and an explicit
body throw. It is a **predecessor close-certificate refusal** test: the existing
seal has `kRegionCoverage`, TryId 4, PC133. Its protected range includes expanded
close copies. II-b does not weaken that proof or claim positive native-exit
coverage for this fixture. Completing those paths needs the earlier close
ownership analysis to certify them first.

SHA256:

- main.js: `e9f3d1e1021f8044914d2a7f85a4c7a87d6c18ea47da9aebee561678fb3b6675`
- main.abc: `b3b476e7310ce947d701350857f9265bb6e66e9d0979f4e910d62ba59532a94c`
- exits.js: `751471a4429b3155959239d8e084bb247683d055b47e0844a37957aec4b9e562`
- exits.abc: `6990ce14b633d11d3ebddc1936ab736dfd6cc43da147d7b3efbb45434586a673`

Increment III adds `for_of_source_cli_test`: it exports this ABC through
`abc_structure --value-source --language-source`, requires a complete record,
and runs Node on that engine output. Zero iterations, last-value live-out,
body-throw closing, next/done/value getters and return getter/call/primitive/
nullish cases are compared by values, exception constructors and traces.
The draft-only test remains separate and is not a publication oracle.

The exported function text is also recompiled using the compiler/options above.
For this pinned parser only identifier-valued string export aliases are unquoted
(`as "scan"` -> `as scan`), following the existing captured-default round-trip
adapter; function text is exact. The resulting ABC is independently imported and
re-proved through the original-flow identity/iteration/events/close analyses.
The explicit-exits predecessor gap remains unchanged.
