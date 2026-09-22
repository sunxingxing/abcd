# Instance-Field Method Value Flow

## Scope

This checkpoint integrates two bytecode shapes used by generated class
constructors and methods:

- `CREATEEMPTYARRAY_IMM8` is a fresh ordinary JavaScript array. Its immediate
  is an inline-cache slot, not an array length, so the source value is `[]`.
- A named method call may spill the property result through `STA_V8` and
  `LDA_V8` before `CALLTHIS`. The source writer fuses the complete four-
  instruction sequence and preserves the receiver and argument registers.

Successful ordinary property reads now produce a language value in the typed
register analysis. This follows the pinned v7.0 `JSObject::GetProperty` path:
missing data properties return `undefined`; getter/proxy abrupt completion is
still a possible throw. Receiver and key operands remain language-only
preconditions. No field identity or heap immutability is inferred.

## Verification

Focused Debug targets passed:

```sh
ctest --test-dir build/decode --output-on-failure \
  -R '^(scalar_test|register_value_flow_test|named_call_test|named_call_js_test)$'
```

The real `Immersive/modules.abc` class fixture produced four executable class
factories, including the member-bearing classes whose methods read constructor
fields (`getInfo`, `getIcon`, `getImage`, and `getIsLive`). The generated module
loaded successfully in Node. The fixture process used 9,088 KiB peak RSS and
0.11 seconds under the 1,000,000,000-byte address-space limit.

The remaining rejected class sites in this fixture are explicit typed failures:
named lexical-environment setup, unsupported body control flow, and other
unproven constructor paths. Constructor property writes remain in source order
and are not hoisted into class declarations or metadata fields.

This increment does not claim complete per-record source output, global binding
identity, full source parameter names/types, or asynchronous function recovery.
