# Computed Property Reads

Reference: pinned `OpenHarmony-v7.0-Release`, including the C++ interpreter
in `ets_runtime/ecmascript/interpreter/interpreter-inl.cpp` and
`RuntimeLdObjByValue` in `ecmascript/stubs/runtime_stubs-inl.h`.

## Supported Mapping

`LDOBJBYVALUE_IMM8_V8` and `LDOBJBYVALUE_IMM16_V8` read a receiver register
and the pre-instruction accumulator as key. Cache slots are encoding fields,
not source operands. Success writes the property value to acc; abrupt
completion does not publish a result. The slow path rejects null/undefined
before `ToPropertyKey`, then performs ordinary `GetProperty`. The fast
operator returns Hole for keys needing user coercion, allowing slow-path
processing instead. These are not own-property-only reads.

`PropertyRead` retains receiver and key as typed values. It is marked as
possibly throwing and must be treated as effectful: key conversion, proxy
traps, prototype lookup and accessors may execute user code. Emission uses
`(receiver)[key]`, preserving native coercion and receiver semantics without
mutable-global helpers. The straight-line emitter materializes the result
in acc before later instructions. The IR denotes the resulting value, not
a Reference that may silently supply `this` to a later call.

Lifting uses fixed time/storage and checks counts, operand kinds, encoded
widths and frame bounds. Emission is linear in referenced name bytes with
fixed scratch and the existing preflight/output budget. There is no heap
snapshot, eager property resolution or constant folding.

## Legacy Evidence Gap

The pinned `DEPRECATED_LDOBJBYVALUE_PREF_V8_V8` handler initially reads its
second register as key. After a fast-path miss it instead reloads the key
from acc before invoking the slow runtime. This is an observable difference
when those inputs differ, not a reason to silently substitute the documented
register key. The legacy opcode remains explicitly unsupported in semantic
lifting pending historical/runtime-path validation. Its decoding remains
supported. `this`, `super`, stores and named/indexed access encodings are not
added by this change.

## Tests and Limits

Tests cover both encodings, typed operands and accumulator destination,
malformed encodings, range limits, uninitialized acc rejection, output
preflight and whole straight-line function emission. Node execution checks
strings and other primitive receivers, null/undefined rejection before key
coercion, Symbol/BigInt/negative-zero keys, proxies, inherited getters,
receiver identity, conversion traces and unchanged slots on thrown keys or
getters. This is not an Ark VM differential proof or validation of platform
exotic objects. Historical semantic profiles and complete application
decompilation remain required.
