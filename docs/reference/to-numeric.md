# Numeric Conversion

The common FunctionTree route also recognizes identity conversion when its
operand already has a non-NaN Number domain proof, or is a checked non-NaN
numeric immediate. It retains the single expression occurrence and combines
original provenance; signed zero and infinities are unchanged. The tree
operand verifier checks the same domain requirement. This is not a general
ToNumeric lowering: Language alone, strings, objects, BigInt and NaN do not
provide this particular identity proof. Their support cannot be inferred from
a following increment or an apparent counter name.

The conditional expansion described below belongs to the older scalar
diagnostic emitter; it is not a common-tree or source-record fallback.

The pinned `OpenHarmony-v7.0-Release` handlers `TONUMERIC_IMM8` and
`DEPRECATED_TONUMERIC_PREF_V8` in `interpreter/interpreter-inl.cpp` read acc
and an explicit register respectively, preserving source registers and
publishing acc only on success. Both return Number/BigInt inputs unchanged.
The remaining path calls `RuntimeToNumeric`, then `JSTaggedValue::ToNumeric`
in `js_tagged_value.cpp`: `ToPrimitive` with number preference, return BigInt
unchanged, otherwise `ToNumber`. Symbols and failing coercions throw.

`UnaryOperator::kToNumeric` expresses this independently of ToNumber. Output
uses a numeric-type conditional and double unary negation for other values:

```js
((typeof input === "number" || typeof input === "bigint") ? input : (-(-input)))
```

The original numeric input is returned without arithmetic. Otherwise the inner
minus performs numeric conversion once; the outer minus restores its numeric
value. Both BigInt and negative zero are retained. No arithmetic addition,
shadowable Number function, property lookup or duplicated user conversion is
introduced. Repeated references are valid only under the emitter's existing
side-effect-free local-slot/literal contract; arbitrary source expressions
must be materialized before using this node. This preserves language values;
NaN payloads produced by user coercion and implementation-specific allocation
failures are not established by this mapping or its Node tests.

Lifting uses fixed storage and emission remains linear in binding-name bytes.
The existing preflight budget prevents partial output on validation failure.
Tests cover both decoded forms, typed input/destination, possibly-throwing
completion, exact/short output budgets, full straight-line bodies, primitive
values, signed zero, BigInt, number-hint custom conversion exactly once,
ordinary valueOf/toString ordering, and thrown-object identity with unchanged
slots. A postfix-update test oracle returns its old converted value; unlike
production output, it intentionally also computes an unused increment.
These tests are not an Ark VM or historical-runtime differential proof.
