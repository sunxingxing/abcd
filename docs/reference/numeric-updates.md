# Numeric Updates

## Common source tree (2026-10-02)

The common ValueProgram route now constructs native `UpdateExpr` and binary
`SequenceExpr` syntax. An update owns a private `UpdateBinding`, distinct from
the input SSA value, result snapshot, parameter, cell, phi and catch staging
storage. Its expression is `(scratch = input, ++scratch)` or the decrement
equivalent. The input is evaluated once; the native operator preserves
Number/BigInt conversion and publishes the new result only on normal completion.
There is no private helper call or addition/subtraction by an untyped one.

Fresh TreeFlow derives the two allowed scratch references from the actual
sequence/assignment/update nodes. One scan rejects other reads/writes, scratch
reuse, observable destinations and reversed evaluation order. Operand checking
independently verifies the original input, direction, prefix result and origins.
An ABC postfix protocol can retain its old converted SSA value separately;
this does not authorize overwriting that old snapshot or inferring a source
reference update from register copies. Native postfix syntax exists, but the
current ValueUnary lowering requires the new result, hence prefix on scratch.

The original importer/domain admission remains binding. Actual decoded positive
cases in this change have proven non-NaN Number inputs/results. The separate
AST/printer Number/BigInt/coercion oracle proves native syntax behavior, not
arbitrary-domain ABC recovery or historical Ark VM equivalence. General
ToNumeric, NaN payload/domain admission and folding a proven source reference
into idiomatic `x++` remain separate work. The scratch comma representation is
not whole-application source-style acceptance.

Decoded tests exercise three profiles, ordinary and phased trees, retained old
values, callback receiver/arguments, catch/throw identity, signed zero, int32
overflow and infinities, and 200/201/400 updates. Syntax mutations and typed
tree mutations check old-value corruption, wrong direction/result, scratch
escape/reuse and evaluation order. Existing source positions and budgets are
retained; no new publication wrapper or writer is required.

## Earlier scalar diagnostic emitter

The pinned `OpenHarmony-v7.0-Release` interpreter implements `INC_IMM8`,
`DEC_IMM8`, `DEPRECATED_INC_PREF_V8`, and `DEPRECATED_DEC_PREF_V8` in
`ets_runtime/ecmascript/interpreter/interpreter-inl.cpp`. All write only acc.
The modern forms read acc; deprecated forms read the explicit register.
Their integer boundary paths promote to double rather than wrap int32.

`RuntimeInc` and `RuntimeDec` in `ecmascript/stubs/runtime_stubs-inl.h`
call `ToNumeric`, propagate abrupt completion, and dispatch to BigInt add/sub
one or numeric increment/decrement. Consequently `x + 1` is not an equivalent
implementation: strings and BigInt require different behavior.

The scalar IR uses typed increment/decrement operators, with `kMayThrow` and
an accumulator destination. The expression emitter uses a private arrow
parameter, for example `((value) => ++value)(input)`, to preserve the input
slot, perform conversion once, and avoid publishing a destination on failure.
The argument is outside the parameter's scope, so its binding cannot be
captured by the private name. This is source syntax, not opcode dispatch or
an interpreter. Later statement/SSA lowering can replace the private call
with an unaliased local temporary; it must not mutate a source register or
commit acc before a throwing conversion completes.

Lifting and emission have constant auxiliary storage, with output time
proportional to the input binding's length. Existing preflight output budgets
apply. The straight-line body emitter consumes these scalar assignments
without a special opcode path.

Tests cover four decoded forms, typed inputs/destinations and completion,
output budgets, primitive results, int32 boundaries, BigInt, numeric-hint
conversion order/count, unchanged input registers, and destination preservation
on a thrown conversion. Node execution validates generated JavaScript;
it is not an Ark VM differential test or proof for every historical runtime.
The private call can affect implementation-specific stack traces, which are
not reconstructed here. Binding/control-flow recovery and per-record output
remain separate unfinished stages.
