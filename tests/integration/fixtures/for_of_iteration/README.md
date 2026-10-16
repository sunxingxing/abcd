# Ordinary key live-out boundary fixture

`main.js` is independent test source; the engine/test analysis reads only
`main.abc`. It exercises a non-captured assignment binding, zero-iteration and
last/break live-out values. It is currently an explicit **ownership gap test**,
not evidence that native for-of or live-out publication works.

Compiler:

```
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
SHA256 4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca
```

From repository root:

```
es2abc --module --merge-abc --opt-level=0 \
  --output=tests/integration/fixtures/for_of_iteration/main.abc \
  tests/integration/fixtures/for_of_iteration/main.js
```

ABC version: 24.0.0.0; method 391 is `iterate`. The decoded protocol has iterator
acquisition, cached next, object check, done/value reads, and expanded close
handlers (the return reads are at PCs 106 and 180). The original loop body
remains ordinary code.

At this checkpoint B cannot seal this method: the first close has
`CloseOwnershipGap::kLiveOut` at original ExceptionNodeId 162; the other has
`kBodyDispatch` at original ExceptionNodeId 106. The gap test strictly requires
`CloseCompletionError::kOwnership`; it never fabricates ownership to run the
iteration proof. Extending that earlier proof is outside this standalone slice.
There is no generated-source Node differential yet. Proxy, prototype mutation,
and cross-runtime iterator effects are not certified by this test.

SHA256:

- main.js: `5d971ec03b4f8662700240356fc6612102b9844701c0ad8d41807a3449f6d13a`
- main.abc: `63256e4546be54992e1fb7a7d35abcea80c9bc872164a5563d150977863e307a`
