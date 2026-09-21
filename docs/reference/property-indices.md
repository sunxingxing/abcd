# Fixed Property Indices

`LiftIndexedRead` models four pinned `OpenHarmony-v7.0-Release` forms:
`LDOBJBYINDEX_IMM8_IMM16`, `LDOBJBYINDEX_IMM16_IMM16`,
`WIDE_LDOBJBYINDEX_PREF_IMM32`, and
`DEPRECATED_LDOBJBYINDEX_PREF_V8_IMM32`.

The first three read acc as receiver. The deprecated form reads its explicit
register on both fast and slow paths, unlike deprecated computed-key loads.
All publish acc only after successful access and preserve registers. Cache
operands do not become source operands. `PropertyIndex` retains unsigned
32-bit values, including values above INT32_MAX, without signed narrowing.

## Source-Lowering Constraint

These nodes deliberately remain distinct from generic `PropertyRead`:

- `ecmascript/interpreter/interpreter-inl.cpp` routes the handlers through
  `FastRuntimeStub::GetPropertyByIndex` and `SlowRuntimeStub::LdObjByIndex`.
- `RuntimeLdObjByIndex` in `ecmascript/stubs/runtime_stubs-inl.h` calls
  the uint32 overload of `JSTaggedValue::GetProperty`.
- That overload in `ecmascript/js_tagged_value.cpp` passes a numeric tagged
  key to `JSProxy::GetProperty`. The wrapper in `js_proxy.h` forwards it
  unchanged; the implementation in `js_proxy.cpp` passes it as a trap
  argument. This differs from JavaScript bracket access's string key.
  Debug assertion behavior and actual interpreter/compiler paths still need
  execution validation; static inspection is not a runtime experiment.
- `ObjectFastOperator::GetPropertyByIndex` in `object_fast_operator-inl.h`
  also casts uint32 to int for number-dictionary lookup. Upper-half indices
  must not be assumed equivalent to positive JavaScript numeric keys merely
  because the encoding is unsigned.

Consequently these instructions are lifted without emitting speculative JS.
Source lowering needs a receiver/path proof or a verified semantic profile;
it must not replace them unconditionally with `receiver[index]`. This is an
unfinished source-recovery requirement, not a declaration that legacy inputs
are out of scope. No input sample is reclassified or skipped on this basis.

## Bounds and Verification

Lifting requires decoded instructions, checks counts/kinds/widths and explicit
register bounds, and takes fixed time/storage with no allocation. Tests cover
all four forms, 0/200/201/65535/2147483648/4294967295 indices where encodable,
the last byte-register and invalid frame boundary, malformed operands, and
unsupported opcodes. They establish IR preservation, not JS or Ark execution
equivalence. The existing source emitter continues to reject fixed-index
instructions rather than silently generate different observable behavior.
