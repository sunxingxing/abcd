# Fresh Record Literals

Pinned v7 `CREATEEMPTYOBJECT` constructs an ordinary object from the realm's
Object constructor. `DEFINEFIELDBYNAME` and `DEFINEPROPERTYBYNAME` call
`RuntimeDefineField`, which creates a writable, enumerable and configurable
own data property and preserves the accumulator. See the pinned
`interpreter-inl.cpp` handlers and `runtime_stubs-inl.h`.

The record writer tracks fresh object and array nodes by typed identity through
scalar register copies. An object buffer supplies ordered primitive properties;
later named DEFINE operations can add constant values or child nodes. Dense
`STOWNBYINDEX` writes append array elements. Parents precede children in
creation order, each child has one parent, and attachment finalizes it. A root
cannot escape before its complete graph is published at module export.
Repeated exports of that root reuse one binding. Whole-record dry emission
prevents semantic and budget failures from publishing partial source.

For function bodies, the bounded fresh-object field planner admits both
`DEFINEPROPERTYBYNAME` and `DEFINEFIELDBYNAME` after a proved fresh object and
stable receiver register. The pinned handlers share `DefineField`; the output
oracle checks own descriptors and inherited-setter exclusion for both forms.
The same planner admits `CALLRUNTIME_DEFINEFIELDBYINDEX` with its encoded
unsigned 32-bit key; it never speculates a dynamic `ToPropertyKey` conversion.
The Node oracle covers array-index ordering and the maximum unsigned key.

The v7 `STOWNBYINDEX` runtime defines writable, enumerable, configurable own
properties and preserves acc. Dense array syntax reproduces those properties
and final length. Computed-key object literal syntax reproduces named
`DefineField`, including an own `"__proto__"` key without changing the
prototype. Buffered `"__proto__"` and duplicate keys remain refused because
their template behavior is not proved equivalent.

Memory admission charges retained keys, values, nodes, and growing field and
element vectors; buffered input and duplicate-check scratch are also bounded.
The output writer limits nesting depth to 256 and charges all whitespace to
the output budget. Work is O(I + B log B + F), where I is entry instructions,
B is each buffered object's property count and F is emitted source bytes.
Array holes, repeated child references, mutation after attachment/publication,
dynamic computed keys, calls and control flow remain refused until alias/effect
proofs are available. Proven string and nonnegative-integer computed keys from
`CALLRUNTIME_DEFINEFIELDBYVALUE` and unsigned keys from
`CALLRUNTIME_DEFINEFIELDBYINDEX` are admitted. Synthetic ABC/Node tests cover
their descriptors, prototype handling, export identity, nested arrays/objects
and output-limit refusal. Immersive's real `Constants` record now emits and
passes a Node route-data oracle.
