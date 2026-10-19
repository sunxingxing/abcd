# Retained input before a private literal selection

`calls.js` is the independent source oracle; only `calls.abc` enters the engine.
The pinned DevEco dynamic es2abc (SHA-256
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`) compiled it with:

```sh
cd tests/integration/fixtures/retained_literal_arm
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 calls.js --output calls.abc
```

The manifest pins both input hashes. The SDK disassembly shows the `produce`
member call before the saved `showToast` lookup, followed by the outer buffered
object, the test of the retained result, two independently constructed literal
arms with array fields, the outer `message` definition and `callthis1`.
`middle` adds ordinary getter definitions around the selected field. `outside`
places the producer before the try region: its exception exits to the caller,
while the argument and invocation still use the original catch.

The pre-change engine refuses this fixture with `kNotInline > kRoot >
kCrossRegionValue`. After the change all three functions export as complete
source and pass `node --check`. The Node differential covers 216 scenarios:
three functions, six truth-domain values, twelve effect/exception modes. It
checks the once-only producer before callee lookup, ToBoolean without valueOf,
selected getter order, throwing producer/callee/arms, non-callable callee timing,
catch vs caller exceptions, receiver identity, callee replacement during argument
evaluation, a changed backing value after the snapshot, descriptors and inherited
setters. No finally-throw oracle or new source-language premise is involved.
