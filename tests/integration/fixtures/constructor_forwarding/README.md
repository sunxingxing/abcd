# Invocation-frame forwarding fixtures

Hand-written TypeScript compiler inputs, generated with the DevEco Studio bundled
es2abc (reported dynamic ABC version `24.0.0.0`) on 2026-10-13. The engine reads
only the ABC; `oracle.mjs` is an independent test-side source-contract oracle.
It uses assignments, deliberately not JavaScript class-field declarations.

From the repository root (substitute `NAME` with `fields` and `pure`):

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 --record-name=NAME --source-file=NAME.ts \
  --output tests/integration/fixtures/constructor_forwarding/NAME.abc \
  tests/integration/fixtures/constructor_forwarding/NAME.ts
```

`constructor_forwarding_test` independently checks `fields.abc`: exactly one typed
`SuperForward` and two live `ValueNamedWrite` assignments, with an actual resolved
class owner. The original constructor entity is 429; `abc_semantic --instructions`
shows the forward at PC 12 and `stobjbyname` at PCs 56 and 91. The compiler's final
transparent rethrow is removed by existing normalization. No user-handler
admission was added. `pure.abc` remains implicit, with identical output under the
default and LanguageSourceV1 contracts.

The Node test compares 480 scenarios against `oracle.mjs`: setters, inherited
non-writable properties, Proxy constructors and returned Proxy objects, primitive
and object parent returns, alternate `new.target`, 0/1/3/200/201 arguments, and
parent/initializer/setter/trap failures. Six source mutations check lost arguments,
lost or repeated assignments, reordered assignments, [[Define]] replacing [[Set]],
and wrong receiver. This validates the source contract, not runtime VM execution
or application/platform behavior. The array-iterator difference is explicitly
accepted by the contract.

SHA-256:

```text
349a651241dfd86cbdaca715b21a346701ece94349958288e6c5d8954d7c6ce8  fields.abc
f6ce96ab3d73db92859a69508cf1ded86b317321a86caa079b95f938b8eac284  pure.abc
```
