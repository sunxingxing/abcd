# Wide range call correspondence (2026-10-22)

The count is an argument count, not a byte width. Raw lifting retains an explicit
ISA-layout attribute on WIDE_CALLRANGE; source evaluation and the independent
tree operand verifier require WideRangeCalls(). LanguageSourceV8 adds precisely
this approved premise. V1–V7 retain their meanings. No array/iterator extraction
or `.call` lookup is introduced.

Pinned paths below are relative to `references/openharmony/arkcompiler/ets_runtime/ecmascript`.

| Variant | C++ interpreter/interpreter-inl.cpp | asm compiler/interpreter_stub.cpp | AOT compiler/bytecodes.cpp | Result |
|---|---:|---:|---:|---|
| WIDE_CALLRANGE | 1483–1489 | 5091–5108 | 1126–1132 | C++/AOT read count bytes 2–3, start byte 4; asm:5097 reads byte 3. V8 premise. |
| WIDE_CALLTHISRANGE | 1393–1399 | 5141–5156 | 991–998 | All agree: count bytes 2–3, receiver byte 4; arguments follow receiver. |
| WIDE_CALLTHISRANGEWITHNAME | 1457–1465 | 5255–5272 | 1012–1020 | All agree: count bytes 2–3, name bytes 4–5, receiver byte 6. |
| SUPERCALLARROWRANGE | 3629–3642 | 4129–4136 | 2159–2165 | All agree: narrow count/start; constructor is accumulator, not invocation function. |
| WIDE_SUPERCALLARROWRANGE | 3765–3778 | 4151–4158 | 2167–2174 | All agree: count bytes 2–3, start byte 4; constructor is accumulator. |
| WIDE_SUPERCALLTHISRANGE | 3493–3506 | 4139–4148 | 2167–2174 | All agree: count bytes 2–3, start byte 4; constructor is invocation function. |
| WIDE_NEWOBJRANGE | 4448–4460 | 5618–5628 | 1344–1350 | All agree: range count includes constructor, arguments begin one register later. |

Each decoder validates operand kinds, width and the complete frame interval
before constructing the existing lazy CallRange. CallOperands independently
checks callee/receiver/argument occurrences against register effects. It has
constant retained storage and linear verification work in argument count;
no argument vectors are allocated by lifting. Existing call-reference,
evaluation, TreeFlow and recovery proofs preserve saved callee, receiver,
argument order, exception exits and normal result Commit.

Arrow-super must additionally prove lexical constructor/newTarget and the
uninitialized receiver cell shared with its enclosing derived constructor.
The raw IR now distinguishes this from ordinary super; it cannot be silently
recovered using the invocation-function proof.

## Acceptance and remaining recovery scope

`build/review/isa-r8-work/roundtrip-final/` records grouping_bytecode.py results
for all 88 new complete fixture records, default and O0 independently. Both
modes have 72 strict whole-record matches and 16 differences (no unverified
compiles). Original exported functions are adapted to the same let-bound
function carrier as the existing template test; identifier-valued string export
aliases are adapted on both sides. Oracle source never enters the engine.
Node syntax passes all 88. The token-only grouping checker is intentionally
not an equivalence oracle for new recovery: all 88 differ in source bindings or
structure, beyond parenthesis removals, so none is claimed as a token pass.

Every difference is retained in `.diff`, not normalized:

- 12 derived fixtures (six counts, two compiler modes in the fixture registry)
  differ only in the constructor body. The original ends with a super statement
  and implicit initialized-this return; recovery uses `return super(...)`.
  es2abc compiles that explicit derived return with an undefined-result decision
  and a receiver-return fallback. A successful super result is an object, so
  the fallback is infeasible; the same object is returned and all arguments,
  invocation effects and exceptions precede the return. Node also compares all
  elements of the returned Array-derived instance, observing every argument.
- Four super-store fixtures have exactly identical method headers, instructions,
  operands and handler ranges. Their differences are physical literal/module
  and string-pool offsets only; every represented literal/module value remains
  the same. These are reported as differences, not byte-identical passes.

The strict occurrence oracle has separate, narrower mapping criteria. For the
84 wide records it records 41 event-sequence mismatches and 26 resource
refusals; for four super-store records it records eight event-sequence and four
missing/ambiguous-method mismatches. No mismatched event or resource refusal is
accepted as round-trip evidence. Node and exact compiled-body comparisons are
reported separately; Ark runtime remains unavailable. The seven new Verified
variants are supported by executed Node record oracles in profile 6, without
transferring evidence to other profiles.

Narrow and wide arrow-super SDK fixtures decode and lift, but remain refused.
Default optimization first encounters CatchRegionError value 1 in the enclosing
constructor's forwarding handler; O0 reaches DerivedProjectionError::kProtocol.
Publishing an arrow requires an interprocedural proof of its accumulator
constructor, lexical newTarget and the same uninitialized-this cell as the
parent constructor. The current invocation-function proof cannot supply it.
This recovery is still unfinished; it is not declared equivalent by V8.
