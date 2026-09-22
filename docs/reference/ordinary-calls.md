# Ordinary Calls

Reference: `OpenHarmony-v7.0-Release`, identities in
`openharmony-v7.0-release.lock.json`. This increment supports modern CALLARG0,
CALLARG1, CALLARGS2, CALLARGS3 and narrow CALLRANGE only. Historical containers
remain readable; historical call semantics are not inferred from modern names.

## Evidence and Mapping

In ets_runtime `ecmascript/interpreter/interpreter-inl.cpp`, CALL_INITIALIZE
(around line 258) takes the callee from acc and checks callability. Fixed
argument macros (324-361) and the range macro (362 onward) preserve argument
order. Fixed handlers (1268 onward) and narrow range (1474 onward) select
`callThis=false`; native and bytecode frame setup use undefined receiver and
new.target. Return replaces acc only on successful completion.

Emit `acc = (0, acc)(r1, r2);`, with explicit register arguments. This calls a
value without a receiver, preserves bound-function and callable-Proxy behavior,
and does not access mutable `.call`, `.apply`, or `Reflect.apply` helpers. No
spread or iterator is introduced. The caller must supply stable, distinct local
bindings holding language values, not VM sentinels. Assignment happens after
the call, so throwing calls preserve acc. Sloppy callee receiver conversion is
performed by the callee, not by the emitter.

The non-callable diagnostic uses `JSTaggedValue::ExceptionToString`
(`js_tagged_value.cpp`, around 871): object descriptions do not invoke user
coercion hooks. Exact diagnostic text is not reproduced. Ark global eval is
unsupported (`builtins_global.cpp`, around 68); this mapping does not introduce
eval or claim equivalence between Ark and Node host builtins.

## Deliberate Refusals

WIDE_CALLRANGE has inconsistent pinned runtime paths: the C++ interpreter
reads the register at pc+4 (`READ_INST_8_3`, around 1483), but assembly
`compiler/interpreter_stub.cpp` around 5092 and baseline
`compiler/baseline/baseline_compiler.cpp` around 4051 read pc+3
(`ReadInst8_2` / `READ_INST_8_2`), the high count byte. Decoding remains
available; semantic lifting and source output refuse it until execution-path
evidence resolves the conflict. Explicit-receiver calls, constructors, apply,
super calls, and legacy calls also remain outside this increment.

## Representation and Cost

`OrdinaryCall` holds either up to three register IDs or a first-register/count
range. Lift validates operand kinds, encoded widths and frame bounds in O(1)
time and fixed storage. Zero count reads no register; a nonempty range may
extend beyond v255 although its encoded first register is eight bits.
Argument access is O(1), and source generation is O(argument count + emitted
bytes), with no temporary argument array. Existing body preflight rejects
semantic/budget failures before publishing text. Accumulator analysis requires
a language value before a call and produces one on the normal successor.

Tests separate C++ representation/bounds/preflight from Node execution of
emitted text. They cover argument order/repetition, 200/201/255 arguments,
200/201/4096/16384 calls, strict receivers, bound functions, callable/revoked
Proxies, classes and other non-callable values, coercion hooks, throwing calls,
and poisoned call/apply properties. Node tests are not an Ark differential
oracle. No wrapper, exception-region recovery, asynchronous task service or
record publication is implemented by this synchronous stage primitive.
