# Effectful parameter-default boundary

`defaults.js` is a new independent fixture. It contains a guarded import-property
initializer, four numeric initializers and a global-property initializer. Its
static self-reference drives the real class family through the member ABI check.
The current expected result is **refusal**, not recovered-source support:
`ClassSelfError::kRecoveryGate / kMemberAbi`, declared length 1 / signature 7.
Neither property expression may be replaced by its base binding or a constant.

Pinned compiler:

```
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
SHA-256 4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca
```

Generation (from the repository root):

```sh
ES2ABC=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
"$ES2ABC" --module --merge-abc --extension=js --opt-level=0 \
  --record-name=member_expression_defaults \
  --output=tests/integration/fixtures/member_expression_defaults/defaults.abc \
  tests/integration/fixtures/member_expression_defaults/defaults.js
```

Committed fixture SHA-256: `5baeddc7a3132ac39a8f6da228cb0c60035c74d254bd39ebf45100266a239e49`.

`member_expression_default_test` checks the decoded production value graph,
actual parameter count, candidate/proof refusal, and resource/work boundaries.
`member_expression_default_source_test` checks the production record failure
and both actual ABI lengths. This is an explicit negative regression. It is
not a Node differential or bytecode-equivalence claim for recovered output.
