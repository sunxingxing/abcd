# Transparent RHS handler beside effectful iterator close

`main.js` is an independent source oracle; engine input is only `main.abc`.
The `source.get()` RHS forces ordinary property/reference/call evaluation before
iterator acquisition. es2abc protects that evaluation with a transparent
same-payload rethrow handler, separately from the effectful iterator close.
Method 391 acquires the iterator at PC47. The production seal must verify the
ordinary handler using `AnalyzeExceptionForwarding`, not claim it as close code.

Compiler: DevEco dynamic es2abc, SHA256
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`:

```
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 \
  --output=tests/integration/fixtures/for_of_forwarding/main.abc \
  tests/integration/fixtures/for_of_forwarding/main.js
```

SHA256:

- main.js: `3088385cefe44c91c96802a32a4351344bea33124ed1fe7551483d0b269502d0`
- main.abc: `6ea996d08ad0176862dcbe694a6b049be8b7eda71747b4c6e746686555ad7677`

The tests drive the production draft, TreeFlow, OriginalFlow and recovery, with
wrong forwarding payload/entry, effectful handler, overlapping close handler,
external normal entry, copy/register/block variants and exact budgets at
200/201/400/800. Node tests engine-generated output, including zero iterations,
last value, RHS getter/call errors, body errors, cached next/receiver and close
priority. The emitted source is recompiled and the full protocol seal and
independent evaluation proofs are rerun. No finally-throw source oracle is used.
The V2 acquisition and V4 external-exception premises remain required. Node
cannot represent a raw internal Hole or verify Ark/native/Proxy runtime behavior.
