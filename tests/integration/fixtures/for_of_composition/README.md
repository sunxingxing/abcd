# Synchronous for-of inside an async body

`async.js` calls an iteration value's method and awaits it. A rejected await is
an abrupt Throw from the loop body. The synchronous iterator's `return` is
called, and any close error preserves that original Throw. No async-iterator
acquisition or promise-valued `return` assimilation is introduced.

`build.json` pins the DevEco compiler/disassembler identities, command and
source/ABC SHA-256. The engine reads only the ABC. Source is evaluated only by
the independent Node oracle and compiler comparison.

`for_of_composition.mjs` checks 60 ordinary-call cases: zero/one/three iterations,
success, throwing method getter, synchronous method throw, rejected await,
normal/null/throwing/primitive iterator close, receiver identity and microtasks.
The existing user-try oracle also now publishes its async fixture and compares
catch/close/await behavior. The finally fixture retains its explicit refusal.

`for_of_async_evaluation_test` uses the production async owner, original-PC
mapping and phased import before the independent close/evaluation verifiers.
It tests moved/duplicated normalization sites, a forged binding disposition,
callee/receiver and missing-exception mutations, register/block variants and
200/201/400/800 scaling with exact admission boundaries.

The SDK round-trip uses `tests/tools/for_of_multiple_roundtrip.py --fixture async
--export collect --async`, which imports `grouping_bytecode.py`'s pinned compiler
and strict disassembly comparison. It preserves every raw difference; it does
not normalize registers, labels, handlers or metadata. The SDK's old export
parser adaptation is applied to both inputs using the existing AST rule.
