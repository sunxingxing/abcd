# Async concurrent fixture

Source written for this test; no paired application source is an engine input.
Pinned dynamic DevEco compiler:

```
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
SHA256 4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca
```

From the repository root, for `functions` and `finally`:

```
es2abc --module --merge-abc --opt-level=0 \
  --record-name=concurrent_async_functions --source-file=functions.js \
  --output=tests/integration/fixtures/concurrent_async/functions.abc \
  tests/integration/fixtures/concurrent_async/functions.js
es2abc --module --merge-abc --opt-level=0 \
  --record-name=concurrent_async_finally --source-file=finally.js \
  --output=tests/integration/fixtures/concurrent_async/finally.abc \
  tests/integration/fixtures/concurrent_async/finally.js
```

ABC SHA256:

- functions.abc: `48e7c5fd190931f057d259420d72071c6c6c9a4820dea3a69ad06900b7df2015`
- finally.abc: `c8c98e3f6bd847403088758a92a2b39de49f3e8e18950ff3031a80ea825597aa`

`concurrent_roundtrip_probe` checks concurrent kind, sendable=false, the actual
ordered import-request annotation, raw AsyncEnter, resolve/reject notification
ports and the async owner. The integration test directly recompiles the engine's
exported record, without rewriting its source, and compares module tables and
method attributes. Notification uses the successful settlement's accumulator
(Promise), not the resolution value used by a native source `return`.

Node runs the original source and actual emitted source as ordinary calls:
success, throws before/after await, rejected await, returned fulfilled/rejected
Promise, catch around await, and microtask traces. Directive erasure is detected
by recompilation; a changed resolution value is detected by Node. Node does not
check Ark taskpool callbacks, cross-thread scheduling or concurrency isolation.

Round 78's original exception-CFG catch-input proof now admits the finally-around-
await fixture. The test independently checks the async/concurrent owner, recompiles
actual engine output, compares kind/annotation and notification completion ports,
and executes fulfilled/rejected ordinary calls with microtask traces under Node.
The finally itself does not throw. No finally-throw source oracle is used:
es2abc's protected inlined finalizers must be compared against ABC handler ranges.
Taskpool/cross-thread behaviour remains unverified by Node.
