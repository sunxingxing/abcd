# Conditional Branch Semantics

Pinned `ets_runtime/ecmascript/interpreter/interpreter-inl.cpp`, handlers
`JEQZ_IMM8/16/32` and `JNEZ_IMM8/16/32` near lines 1161-1233, explicitly test
tagged booleans and numbers. The lifting API returns a taken-edge predicate,
not an assignment, and does not mutate the accumulator.

For an already evaluated language value in local binding `a`:

```js
// jeqz
(a === false || a === 0)
// jnez
(a === true || (typeof a === "number" && a !== 0))
```

These predicates are not complements and not general ToBoolean conversions.
NaN takes jnez. Both signs of zero take jeqz. Null, undefined, strings, BigInts,
symbols and objects take neither. No coercion or property access occurs.
On a proven boolean-only input they may later simplify to logical tests, but
that requires a data-flow proof, not an assumption from a branch opcode name.

`LiftBranchCondition` supports the six encodings above. The source emitter uses
typed unary predicates and the existing checked scalar-expression renderer.
Repeated references are valid only because input values are side-effect-free
local slots/literals; arbitrary source expressions must not be substituted.
The caller must exclude internal Hole/sentinel values and validate branch
targets separately. Whole-function structuring remains pending.

The C++ interpreter's jstricteqz/null/undefined/register comparison handlers
near lines 6935-7048 are fatal unimplemented stubs. This alone does not prove
that every other execution tier lacks these instructions. They remain explicit
unsupported lifting results until relevant implementation evidence is obtained;
their names are not sufficient semantic evidence.

Tests decode all six widths, check predicate identity and malformed operand
count, and execute generated JS against independent tagged-type dispatch in
Node. Inputs include signed zero, NaN, infinities, primitives, objects and a
proxy that throws on any property access. Emission tests check exact output
budget and no partial output on validation failure. These are source predicate
tests, not Ark execution differential tests or historical-equivalence proof.
