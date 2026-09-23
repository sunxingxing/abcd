# Instance-Field Method Value Flow

## Scope

This checkpoint integrates two bytecode shapes used by generated class
constructors and methods:

- `CREATEEMPTYARRAY_IMM8` is a fresh ordinary JavaScript array. Its immediate
  is an inline-cache slot, not an array length, so the source value is `[]`.
- A named method call may spill the property result through `STA_V8` and
  `LDA_V8` before `CALLTHIS`. The source writer fuses the complete four-
  instruction sequence only when the spill register is neither receiver nor
  argument and an immediate terminal return/throw proves it dead afterward.
  Other spill patterns require shared liveness before admission.

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

## Integration Correction

Review of the implementation found that the lexical writer emitted individual
instructions while its plan omitted CALLTHIS, losing the actual call. The plan
now retains a typed NamedInvocation at the property read; the writer emits that
complete operation once. Spill/reload instructions are omitted only after the
terminal dead-register check above. Regressions cover direct/spilled lexical
emission, overwritten receiver, callee passed as argument and later callee use.

An uncommitted globalThis lowering was removed during review: global lexical
records, missing-name exceptions, const writes and initialization are not
equivalent to object-property accesses. Global source integration still needs
binding resolution and explicit operation-specific semantics.

Correction verification: full Debug CTest passed 108/108 in 31.43 seconds.
The five paired class fixtures admitted 11 of 101 creation sites (4 Immersive,
5 MultiCommunityApplication, 1 pasteboard, 1 push modules, 0 widgets).
`class_samples.mjs` passed on all 11 expressions, including constructor argument
identity through the four recovered field-read methods. A separate Immersive
run measured 9,216 KiB peak RSS and 0.10 seconds under the fixture's 1 GB cap.
These are class-expression tests, not complete record or Ark runtime differential
validation. Sanitizers were not rerun for this correction.
