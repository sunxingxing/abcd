# Mixed iteration context diagnostic

`scan` (m395) contains a for-in followed by a for-of. Both acquisition-only
handlers transparently rethrow; the for-of also has effectful close regions.
The engine receives only main.abc. main.js records the independent source, not
an engine input. Round 83 deliberately retains the production refusal: the
shared forwarded handler does not yet have a joint source owner. This is a
located-refusal test, not an engine-output Node differential or completion.

Compiler: DevEco dynamic es2abc, SHA256
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

```
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 \
  --output=tests/integration/fixtures/iteration_context/main.abc \
  tests/integration/fixtures/iteration_context/main.js
```

The unit tests verify both evaluation protocols, missing proof-input/plan
mutations, the existing iterator/event/port mutations, copy/register/block
variants and prepaid 200/201/400/800 scale/budget boundaries. The CLI test
checks the acquisition opcode and compares manifest and abc_semantic witnesses.
JSON unit tests cover both serialization paths, absent locations and real PC0.

SHA256:
- main.js: `8a92e0290de24fd92c09d7da02e6bdcc11165877937143970c2f87ff5d10b904`
- main.abc: `da5509ba887028ebc6719bc5e649b0c584847148ea766b3c2e40c15e808f0e5f`
