# Fresh Record Objects

Pinned v7 `CREATEEMPTYOBJECT` constructs an ordinary object from the realm's
Object constructor. `DEFINEFIELDBYNAME` and `DEFINEPROPERTYBYNAME` call
`RuntimeDefineField`, which creates a writable, enumerable and configurable
own data property and preserves the accumulator. See the pinned
`interpreter-inl.cpp` handlers and `runtime_stubs-inl.h`.

The record writer now tracks a fresh object by typed identity through scalar
register copies. It accepts only constant primitive values and string-pool
keys, no escape before module export, and no write after the first export.
Definition order is retained. A computed-key object literal reproduces the
data descriptors, including an own `"__proto__"` key without changing the
prototype. The first export materializes one binding; subsequent exports of
the same object reuse it. Whole-record dry emission prevents semantic and
budget failures from publishing partial source.

Memory admission charges retained keys, values, object states and growing
field vectors; output is bounded independently. Work is O(I + F), where I is
entry instructions and F is emitted field/value text. The implementation
does not currently admit nested object/array values, buffered objects,
mutation after publication, computed keys, calls or control flow. These
require a typed heap graph with alias and effect proofs, not textual
substitution. The synthetic ABC and Node oracle test names, descriptors,
prototype handling, export identity and output-limit refusal.
