# Initialized capture guards in decision prefixes

`guards.js` is an independent Node source oracle; the engine only consumes
`guards.abc`. Produced by the DevEco SDK es2abc on this machine, ABC 24.0.0.0:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 --record-name=guards --source-file=guards.js \
  --output tests/integration/fixtures/decision_guards/guards.abc \
  tests/integration/fixtures/decision_guards/guards.js
```

SHA-256: `7955a5b3d5db6ec7f2ee2f58ef96a67172c6ec330ec50b0ac0c4e4d80fe26549`.

Method 473 (`run`) has a real user handler, two captured reads with named hole
checks and a short-circuit decision. Before the Round 45 emitter change it
refused at original/working PC 89, Op37, `kMissingExpression`, placement
`kCellGuard`. This is an initialized premise, not the throwing guarded-read
projection from Round 38. The CLI test checks these opcodes and dispatch edges
before running the oracle.

The differential covers two getter/valueOf evaluations, short-circuiting,
rebinding the cell between terms, getter/valueOf/property throws, body-call
throws, catch and finally traces under both contracts. It compares exception
constructors, return values and traces, never error messages/stacks. Three
mutants per contract duplicate evaluations, reverse the boolean connective or
replace the live right-hand cell read; every mutant must be detected.

Finally-throw uses a bytecode-derived oracle, not the original JS oracle.
In `guards.abc` method 473, the try-path inlined finalizer at PC 144–170 is
still protected by the user catch H247 (the call at PC170 has that exception
edge). The catch-path finalizer at PC 299–325 is protected by catch-all H337
(the call at PC325 targets H337). Its finalizer call at PC377 propagates to
the caller. With the pinned interpreter's first-matching-try dispatch, the
expected trace suffix is exactly `"finally", "EvalError", "finally", "finally"`,
with a final EvalError. The test asserts these three dispatch edges and the
entire result, counts and trace, under both contracts. This is an es2abc
lowering divergence from the original JS protected ranges, not a recovery bug.
The other 22 executions retain the independent source oracle. Six mutations
must still be detected; a mismatch in an unmutated execution cannot count as
mutation detection. Round 44's `guarded_calls` CLI currently has no throwing
finally mode, so its existing seven modes per contract remain unchanged.
