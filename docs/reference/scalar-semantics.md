# Scalar Semantic IR

`LiftScalarInstruction` adds a typed pre-SSA expression boundary, not an assembly
printer or a complete method decompiler. It borrows one decoded instruction and
returns a destination slot, a literal/copy/unary/binary expression, and a
completion effect. It performs no allocation, constant folding or host-language
coercion. Inline-cache operands are validated as encoding fields but do not become
source operands. Work is bounded by the fixed encoded operand count.

Reference: pinned OpenHarmony-v7.0-Release `isa.yaml`,
`ecmascript/interpreter/interpreter-inl.cpp`, `slow_runtime_stub.cpp`,
`fast_runtime_stub-inl.h`, and `stubs/runtime_stubs-inl.h`.

## Covered Forms

The current mapping covers 44 encodings:

- Twenty binary arithmetic, equality, comparison, shift and bitwise operations.
  The register is the left input and the accumulator is the right input.
- Eleven unary encodings: modern negation, bitwise complement, ToBoolean,
  logical negation, typeof and ToNumber, including verified wide/cache and
  deprecated register-input forms where applicable.
- Three move encodings plus accumulator load/store.
- Signed 32-bit integer and raw binary64 immediate loads.
- Undefined, null, true, false, canonical NaN and positive infinity loads.

`shr2` maps to unsigned right shift, `ashr2` to signed right shift, `not` to
bitwise complement, and `istrue/isfalse` to truth conversion/negation. Operands
are already-evaluated slots: their later source expressions must not be duplicated
or reordered, especially where ToPrimitive hooks can execute user code.

Integer immediates use a bit-preserving signed conversion. Floating immediates
retain every encoded bit, including negative zero and NaN payloads, without any
host floating conversion. Deciding how those bits become source literals remains
the responsibility of a separately verified emitter.

Completion metadata describes language-level normal/possibly-throwing completion,
not VM allocation failure or asynchronous host interruption. A throwing RHS must
not publish its destination. These expressions require language values; VM hole,
environment and other internal values must be eliminated or modeled explicitly
before JS source emission. `ldhole` therefore does not map to undefined.

## Deliberate Gaps

The pinned C++ `DEPRECATED_NEG_PREF_V8` handler lacks the modern INT32_MIN guard.
It is explicitly unsupported until execution-mode/profile-specific behavior can
be established, rather than silently equating it with JS unary minus.
Increment/decrement and ToNumeric also remain unsupported: `x + 1` and unary `+`
are not valid generic replacements because string and BigInt behavior differs.

Other opcodes return a typed unsupported result. A caller must not replace these
with empty statements or report partial expression mapping as completed function
or record recovery. There is no source file output in this increment.

Tests cover all mapped encodings, operand order, explicit source/destination
slots, IC exclusion, INT32_MIN literals, signed-zero bits, malformed operands,
frame bounds and unsupported VM/legacy cases. These are structural IR tests, not
execution differential tests against Ark. Native fast-path edge cases (including
signed arithmetic), historical runtime compatibility, coercion traces, exception
state, complete SSA and JS/TS/ArkTS behavioral equivalence remain to be verified.
