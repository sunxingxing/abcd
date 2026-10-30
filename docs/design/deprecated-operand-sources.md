# Deprecated Operand Sources

## Semantic Contract

Most deprecated (`0xfc` prefix) instructions run the same runtime operation as
their current form. The only difference is where one operand comes from: a
register instead of the accumulator. They lift to the same typed operation,
and that operation records the operand's source slot. They never get a
separate operation or a separate source form:

| Deprecated form | Operation | Slot that becomes a register |
| --- | --- | --- |
| `CALLARG0/1`, `CALLARGS2/3` | `OrdinaryCall` | callee (first register) |
| `CALLRANGE imm16, v` | `OrdinaryCall` | callee `v`, `imm16` arguments from `v+1` |
| `CALLTHISRANGE imm16, v` | `ReceiverCall` | callee `v`, receiver `v+1`, `imm16-1` arguments from `v+2` |
| `CALLSPREAD v0, v1, v2` | `ReceiverCall` (apply array) | callee `v0` (receiver `v1`, array `v2`) |
| `LDOBJBYNAME id32, v` | `NamedRead` | receiver `v` |
| `DELOBJPROP v0, v1` | `PropertyDelete` | key `v1` |
| `DYNAMICIMPORT v` | `DynamicImport` | specifier `v` |
| `INC/DEC/NEG/NOT/TONUMBER/TONUMERIC v` | scalar unary | input `v` |
| `COPYDATAPROPERTIES v0, v1` | `CopyProperties` | source `v1` |

Register effects, call operands and value operands read the recorded slot.
The value IR is therefore identical to the current form, and the source route
needs no deprecated-specific code. Fused property-call matchers
(`named_invocation.cc`) prove that the loaded property flows through the
accumulator into the call. They accept only accumulator callees and receivers.
Register-sourced forms reach source only through the generic SSA value route.

Pinned references are in `ecmascript/interpreter/interpreter-inl.cpp`:
`DEPRECATED_CALL_INITIALIZE` and `DEPRECATED_CALL_PUSH_ARGS_*` (234-405, with
handlers at 1275-1497), `DEPRECATED_CALLSPREAD` (1612, same `CallSpread` as
`APPLY` at 1596), `DEPRECATED_DELOBJPROP` (5346), `DEPRECATED_LDOBJBYNAME`
(6569) and `DEPRECATED_DYNAMICIMPORT` (7580, same `DynamicImport` as 7570).

`DEPRECATED_LDOBJBYNAME` narrows its ID32 operand to a `uint16_t` pool slot.
Only ids at or below `UINT16_MAX` are admitted, so the regional slot the
engine resolves is the one the runtime reads.

## Rejected

- `DEPRECATED_LDOBJBYVALUE` stays unsupported. Its fast path reads the key
  register, but its slow path reads the accumulator (5661-5662), so the key
  is not a single proven value.
- `DEPRECATED_SETOBJECTWITHPROTO` preserves an accumulator that is not the
  target object. The current `CopyProperties` set-prototype contract writes
  the target to the accumulator, so this form needs its own result rule first.
- `DEPRECATED_LDSUPERBYNAME`/`LDSUPERBYVALUE` would fit the same slot model.
  They wait for a constructed home-object fixture rather than shipping
  without a differential.
- `.call`/`.apply` synthesis for unfused receiver calls stays rejected, as for
  the current forms (see `named-invocations.md`).

## Evidence

`tests/isa/deprecated_source_test.py` builds one module per form and profile
(DYNAMIC1206, DYNAMIC2400) with the test-only writer. Each module exports
`f`, which applies the instruction to parameter registers.
`tests/integration/deprecated_source.mjs` compares the emitted module with
equivalent JavaScript over a per-family input set: results, settled
promises, errors, and observable conversions, getters, proxy traps and call
receivers. Spot mutations (wrong receiver, extra argument, `Object.assign`
for spread, string-converted delete keys) are detected.
