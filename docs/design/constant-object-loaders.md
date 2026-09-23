# Constant object loaders (OpenHarmony v7)

Reference: pinned `OpenHarmony-v7.0-Release` `arkcompiler/runtime_core/isa/isa.yaml`
group `constant object loaders`, checked against
`arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp`.
`LiftConstantObjectInstruction` validates all 18 zero-operand encodings and
records accumulator, context and abrupt-completion effects as typed values.
The result is an instruction-level semantic fact, not blanket permission to
publish executable source. Tests decode each v7 wire form, reject fabricated
operands and compare the pinned handler effects.

| Instructions | VM value or effect | Source admission |
| --- | --- | --- |
| `ldnan`, `ldinfinity`, `ldundefined`, `ldnull`, `ldtrue`, `ldfalse` | Replace accumulator with the corresponding primitive | Scalar expression emitter; decoded-byte and Node oracles |
| `ldsymbol` | Load the realm's original Symbol constructor from global environment | Refused: a mutable `Symbol` or `globalThis.Symbol` lookup is not equivalent |
| `ldglobal` | Load the VM global object | Refused: ordinary JS identifiers can be shadowed or rebound |
| `ldhole` | Load internal Hole sentinel | Record guard analysis only; never print as `undefined` |
| `deprecated.ldlexenv` | Load current VM lexical environment object | Omitted only under the local lexical planner's environment and escape proof |
| `ldnewtarget` | v7 interpreter handler is `FATAL`/unimplemented | Refused; an emitted `new.target` would invent an executable behavior |
| `ldthis` | Load frame `this` | Emit `this` inside admitted callable bodies; receiver-identity Node oracle |
| `poplexenv`, `deprecated.poplexenv` | Pop current VM environment; preserve accumulator | Omitted only under the local lexical planner's environment proof; deprecated ISA output flag is misleading |
| `getunmappedargs` | Allocate unmapped arguments object from actual frame arguments; may throw | Refused until argument count, mapping and identity are proven |
| `asyncfunctionenter` | Allocate async completion promise; may throw | Removed only by `ProveAsyncCompletion` after verifying the matching resolve/reject/handler protocol |
| `ldfunction` | Load current JSFunction identity | Refused unless callable self-binding identity is separately established |
| `debugger` | Notify debugger, leave accumulator unchanged | Emit `debugger;` in admitted function and record bodies; decoded-record and Node oracles |

`ldthis` and `debugger` are included in both straight-line emission and
accumulator-flow verification for ordered bodies. The other context values
remain typed refusals in the plain linear writer; specific planners may consume
them under their own proofs. Historical ABC uses the same format decoder, but
source admission remains constrained by the enclosing profile and method proof.
The lift is O(1) per instruction and stores no per-method state.
