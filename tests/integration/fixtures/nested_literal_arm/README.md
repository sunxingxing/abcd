# Selection stored into an enclosing private literal

`calls.js` is an independent source oracle, never an engine input. `calls.abc`
was compiled by the pinned DevEco dynamic es2abc with:

```sh
cd tests/integration/fixtures/nested_literal_arm
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 calls.js --output calls.abc
```

The compiler SHA-256 is
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
The manifest pins both input hashes and the options. The SDK disassembler shows
that each method saves the `showToast` member Reference, allocates the outer
buffered object, branches to two buffered objects with array initializers,
commits the joined result with `definepropertybyname "message"`, and invokes
`callthis1` using the original saved receiver. The original try covers the
argument construction and invocation; its catch calls `input.caught`.

`middle` also defines `before` and `after` on either side of the selected field.
This tests that an enclosing literal's other definitions retain their order.
The engine must export both functions as complete source, and that source must
pass `node --check`. The integration runner compares 120 separate scenarios
with the original module: both branches, ordinary values, callee lookup/call
failures, field getters and throws, catch throws, receiver identity, replacement
of the callee during argument evaluation, descriptors, and inherited setters.
These are engine-output differentials, not just source-oracle checks.
