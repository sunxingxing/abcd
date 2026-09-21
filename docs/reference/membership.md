# Membership Tests

## Property Membership

The pinned `OpenHarmony-v7.0-Release` `ISIN_IMM8_V8` handler in
`ecmascript/interpreter/interpreter-inl.cpp` reads the key from its register
operand and the target object from acc, then writes acc only on successful
completion. `RuntimeIsIn` in `ecmascript/stubs/runtime_stubs-inl.h` first
requires an ECMA object, then converts the key with `ToPropertyKey`, then
calls `HasProperty`. It does not coerce primitive targets into objects and
does not read the property's value.

`BinaryOperator::kIn` preserves this order and maps to `(key in object)`.
Its completion is `kMayThrow`; key coercion and a Proxy `has` trap may execute
user code. It is not a pure own-property check, a truth test of a property
value, or an invocation of a shadowable global helper. Acc is published only
after the operation completes. The encoded cache slot is not a source operand.

Existing constant-storage lifting and preflight emission remain applicable.
Tests cover operand roles, cache/register byte widths, frame bounds, source
body integration, primitive target rejection before key coercion, inherited
properties without invoking getters, Symbol/BigInt/negative-zero keys, Proxy
traps and invariants, and preservation of both slots on abrupt completion.
Generated expressions run under Node; this is not an Ark differential test
or proof for platform-specific exotic objects.

## Instance Membership Gap

`INSTANCEOF_IMM8_V8` is not yet mapped to JS `instanceof`. The pinned C++
interpreter has distinct cached and uncached paths:

- `RuntimeInstanceof` calls `JSObject::InstanceOf` in `js_object.cpp`, whose
  custom `@@hasInstance` result is explicitly converted with `ToBoolean`.
- `RuntimeInstanceofByHandler` in `runtime_stubs-inl.h` returns the custom
  handler's tagged result directly; the cached interpreter path writes this
  result to acc without a boolean conversion.

Thus a non-boolean custom result can distinguish these paths according to
source inspection. Cache configuration, actual execution mode and historical
runtime behavior require validation before choosing a semantic profile or
proving the difference unreachable for a recovered method. The scalar lift
continues to return an explicit unsupported result, with a test checking the
intended opcode. No sample is reclassified or counted as decompiled because
of this limitation. Full source recovery remains unfinished.
