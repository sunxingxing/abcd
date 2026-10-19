# Guarded binding arguments under an unmet member reference

`arms.js` tests module-local captured let/const reads before and after their
native lexical initialization, including captured bindings used as callees.
Its early captures require the existing SourceTdz contract; previous flags keep
the located ScopeError::kInitialization refusal. `initialized.js` creates its
closures after the stores and completes under both flag sets. Each also reads
a real imported receiver from `./dependency.mjs`; the test supplies the same
independent dependency to engine output and source oracle.

The engine takes ABC only. Both inputs contain branch-local lexical loads and
named Hole guards feeding a phi and saved member call. The production pipeline
exports the record; Node compares values, exception constructors and complete
traces. Early callbacks see ReferenceError before argument evaluation; condition
getters/throws, invocation throws, mutable outer cells, receiver identity and a
callee overwritten by the condition getter are exercised. The captured-callee
case keeps ordinary phi materialization; it is not an additional admission.
Five detected mutations per published output remove/change an arm read, repeat
the callee getter, detach this, or move the selected read before the condition.

`dependency.mjs` exports the already-initialized external receiver. A cyclic
module that calls a hoisted exported function before its creator evaluates is
not claimed here. Uninitialized external/module guard correspondence remains
covered by the existing module-guard tests; this slice adds no module policy.

Reproduce from the repository root, with the pinned DevEco compiler:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 --record-name=arms --source-file=arms.js \
  --output tests/integration/fixtures/guarded_reference_arms/arms.abc \
  tests/integration/fixtures/guarded_reference_arms/arms.js
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 --record-name=initialized --source-file=initialized.js \
  --output tests/integration/fixtures/guarded_reference_arms/initialized.abc \
  tests/integration/fixtures/guarded_reference_arms/initialized.js
```

Node does not execute ABC. No Ark/Node message or stack-text equality is claimed.

SHA-256:

```
4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca  /Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
2d393a9351fbcc3ce33a692b0385c1a0a815aab139a7ad73cd9f9b21d5fd919e  tests/integration/fixtures/guarded_reference_arms/initialized.abc
075adb71463a4a9f490cf3b796388149a8899aa5e87fb4e38207fa23064273b4  tests/integration/fixtures/guarded_reference_arms/arms.abc
```
