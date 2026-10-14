# Native for-in protocol fixture

`loops.js` is an independent evaluation source, never an engine input.
`loops.abc` was generated locally with DevEco Studio's es2abc:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 --record-name=loops --source-file=loops.js \
  --output tests/integration/fixtures/for_in/loops.abc \
  tests/integration/fixtures/for_in/loops.js
```

SHA-256:

- Compiler: `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`
- loops.js: `244877ab3c90b120ccd8d2defc00588707a343b68ab378f6e97a278a218768af`
- loops.abc: `eb4372980abc0d096e8ac8794218e40de42449254b67f61454d3ddc351953405`

`for_in_fixture_test` decodes and imports the file, checks typed Acquire/Next
operations and their acquisition-only exception handlers, builds and rechecks
the raw protocol evidence. It covers scan, break/continue/return/throw and
nested loops. It also checks the capture-environment operation in `captured`;
that method deliberately remains `kAcquisitionOrder`. Its environment is
allocated at PC10 before acquisition at PC14, then popped at PC26 before
advancement at PC27. A distinct iteration environment is allocated at PC39
after the done test, and the key is stored at PC47. The acquisition handler
also pops the environment (PC21) before rethrowing. These scope transitions
need the binding-activation and RHS-scope projection; the test does not erase
them or treat an environment pop as inert.

Verified acquisition/next PCs: scan m440 16/27; exits m468 18/29;
nested m496 20/31 and 49/60. Captured m524 is refused at working PC27.

At the historical B/C increment only raw protocol evidence was verified. The
engine-output differential is now provided by the separate native.abc fixture
below. It covers ordinary objects; Proxy trap sequences, prototype/enumerability
mutation and the Proxy Symbol-key path remain explicit pinned-Ark verification
gaps. Source-only execution is not evidence of Ark equivalence.

## Increment III production fixture

`native.js` / `native.abc` use the same pinned DevEco compiler and command above,
with `--record-name=native --source-file=native.js` and native input/output
paths. `key_break.js` / `key_break.abc` use `--record-name=key_break
--source-file=key_break.js`; the remaining flags are identical. The source
files are independent oracles and never engine inputs.

SHA-256:

- native.js: `a1a781941f2f0c0f365e1f740e22586f48df2b45af79364a3b70e43418890d6f`
- native.abc: `e5069232c2d56a764ea333aca9f825da7c4a4545fe85195b51a020c57791cce7`
- key_break.js: `ddac0a1ab594c68574b55cf0b213c1d65c23d7b6e20a68e92452b83b49bb1323`
- key_break.abc: `cd2dfab3766a833965f7cc1ecff78112bcfb42a509c6ed1aafa7ee8515cff454`

Verified acquisition/next PCs in native.abc: keys m421 18/29, scan m449 16/27,
exits m477 18/29, nested m505 20/31 and 49/60, keysBreak m561 28/39.
The IR fixture test checks actual instructions, acquisition handlers, and the
same production builder plus OriginalFlow/recovery verification. The Node test
exports engine output under both flags and checks syntax and independent
ordinary-object traces. It also detects mutations replacing inherited enumeration
with Object.keys and losing the zero-iteration local key.

`keys` covers local key live-out for zero and last iterations. `keysBreak`
covers property-target key live-out including early break. `key_break` is a
negative: local-key live-out with break and nonempty exhaustion-edge phi copies
must remain `ForInError::kExit`, m380@22. Those copies cannot run on a break
path. This test does not claim that the current representation supports that
case. Captured-key m524 in loops.abc remains refused. No Proxy/prototype or
enumerability-mutation/Symbol-path Ark equivalence is inferred from Node.
