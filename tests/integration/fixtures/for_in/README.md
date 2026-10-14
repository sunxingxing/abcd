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

This B/C increment does not emit ForInStmt. There is therefore **no Node
differential of decompiler output yet**; that belongs with the tree/recovery/
printer increment. The fixture source is prepared for that oracle, not a
substitute for it. Its future Node differential must cover ordinary objects,
key order, inherited/shadowed keys, deletion and ordinary control/exception
traces. Proxy trap sequences, prototype/enumerability mutation and the Proxy
Symbol-key path remain explicit pinned-Ark runtime-verification gaps. Do not
label a Node source-only test as Ark equivalence or engine recovery evidence.
