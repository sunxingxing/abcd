# Literal commits in compound predicates

`conditions.js` is compiled input, never supplied to the decompiler. The engine
exports from `conditions.abc`; the independent Node oracle uses the original JS.
On 80515095 this record stops at m437 (`branch`), TreeFlowError::kPlacement:
the second condition contains a private buffered literal and its own-definition
commits. The fixture exercises both disjunction and conjunction, an if statement,
selected return values, and a nested literal. It preserves the same event family
as o2/10579 m11217 without its UI/capture dependencies.

The differential compares values, exception constructors and full effect traces
for flags/results, throwing flag/callee/argument/body/arm getters, a callee
replaced during argument evaluation, receiver identity and a prototype setter
that must not run for the literal's own property. Both source contracts run
144 scenarios each. Six actual source mutations per output must be observed;
none is accepted merely because a replacement regex matched. Node runs generated
source, not ABC; no Ark runtime execution is claimed.

Pinned DevEco compiler SHA-256:
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

Reproduce from the repository root:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 --record-name=conditions --source-file=conditions.js \
  --output tests/integration/fixtures/decision_literal/conditions.abc \
  tests/integration/fixtures/decision_literal/conditions.js
```

`manifest.json` pins both source and ABC hashes. The C++ test separately constructs
and decodes equivalent register-copy/block-split forms, rechecks initial semantics
and all recovery projections, mutates ownership/origin/order/branch disposition,
and checks exact byte/work boundaries at 200/201/400/800 independent predicates.
