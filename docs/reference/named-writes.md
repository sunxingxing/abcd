# Named Property Writes

`LiftNamedWrite` models the two modern STOBJBYNAME encodings, retaining the
method-regional string pool slot and receiver register. The value is the
accumulator; both register and accumulator survive successful execution.
Cache operands are validated but do not become language operands. Own-property
definitions, computed/indexed writes and historical encodings are not inferred.

## Pinned Evidence

OpenHarmony-v7.0-Release `ecmascript/interpreter/interpreter-inl.cpp:3037-3181`
restores the accumulator on IC, fast and slow paths. `RuntimeStObjByName` in
`ecmascript/stubs/runtime_stubs-inl.h:773` calls SetProperty with throwing
semantics. `ObjectFastOperator::SetPropertyByName` in
`ecmascript/object_fast_operator-inl.h:427` preserves the receiver for setters
and Proxy traps and throws on read-only properties. The IC miss path in
`ecmascript/ic/ic_runtime.cpp:394` also uses throwing SetProperty for exotic
and non-object receivers. These paths were inspected, not executed in Ark.

## Source Mapping

The shared linear/acyclic body writer emits a private strict arrow invocation:

```js
(($object, $value) => { "use strict"; $object["name"] = $value; })(r0, acc);
```

Arguments are evaluated outside the private parameter scope, so matching names
in the caller do not capture/shadow them. No mutable global helper or intrinsic
lookup is needed. The write remains strict in a sloppy enclosing function.
Its result is discarded, preserving acc even if a setter returns another value.
Receiver and value names remain side-effect-free local bindings under the body
writer's existing contract. Accumulator analysis requires a language value and
marks the operation as preserving it; Hole is never converted to undefined.

Lifting is O(1), allocation-free. Emission adds O(log R + K + output bytes) per
resolved name, with R regions and K UTF-16 units. Constants and output retain
their existing budgets and two-pass no-partial-semantic-output behavior.

## Indexed-Name Refusal

The pinned name fast path does not classify ordinary element keys: Arrays are
excluded by IsSpecialIndexedObj, and AddPropertyByName writes named-property
storage without element lookup or array length updates. Named reads likewise
do not consult element storage. A native JS bracket operation would change this
behavior for a canonical element name. IC/slow paths can classify the key, so
assuming one execution path or ordinary-object receiver is not sufficient proof.

Both named read and write emission now return `BodyEmitError::kIndexedName` for
canonical ASCII decimal names from `0` through `4294967294`. Classification
matches the pinned ToElementIndex/StringToUint boundary: leading zeros, signs,
fractions, empty names and `4294967295` are not ordinary element names. This
guard applies after string resolution, not to the typed opcode lift or ordinary
string literals. It is a conservative source refusal, not malformed ABC.

## Verification

The source test first failed on unsupported writes. Both encodings now pass
linear and acyclic emission tests, output-budget refusal and uninitialized
accumulator checks. Lift tests cover 200/201 slot/register boundaries, maximum
pool/cache fields, malformed operands, register range and distinct own-property
opcodes. Name tests cover both indexed boundaries and noncanonical alternatives;
disabling the guard makes the regression fail, restoring it passes.

Node executes emitted bodies in module and reparsed sloppy contexts, checking
values including NaN/-0/BigInt/Symbol, inherited setters, direct/inherited Proxy
receiver and key identity, false/throwing traps, read-only/nonextensible targets,
primitive rejection and primitive inherited setters. `$object`/`$value` caller
names exercise hygiene. The complete Debug suite passed 62/62; named_write_test
and linear_constants_test passed ASan/UBSan. Independent review verified the
strict-write paths and exposed the indexed-name mismatch, which is now guarded.

These are function-body semantics and JS execution tests, not a complete
record writer, whole-application recovery, or historical-runtime equivalence.

Full-corpus conditional body counts and process memory are recorded in the
[validation report](../validation/2026-09-22-named-writes.md).
