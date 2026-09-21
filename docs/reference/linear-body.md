# Straight-Line Function Body Emission

`WriteLinearBody` connects decoded instructions to checked scalar assignment
expressions, return statements and throw statements. This is a bounded source
primitive, not a complete decompiler or an alternative definition of success.
The caller still needs to recover the function kind, declarations, ABI bindings,
module/record placement, names, dependencies and control-flow structure.

Supported bodies contain the scalar lifter's supported assignments, nop,
return, returnundefined and unconditional throw. Unsupported instructions,
exception regions, absent terminators and instructions after a terminator
produce typed failures with bytecode offsets. Nothing is silently discarded
except semantically inert nop. Branches require future structured emission.

The caller supplies distinct writable local bindings for registers and the
accumulator. Registers must be initialized from the recovered ABI and contain
language values. No helper call, property access, arbitrary expression or VM
sentinel is allowed in these bindings. The accumulator starts uninitialized
in this analysis; reads before a supported write fail rather than changing Hole
to undefined. Accumulator dependency checks inspect typed RHS values instead
of relying on ISA accumulator flags that are inaccurate for some old opcodes.

Assignments remain in original order. JS assignment evaluates the RHS before
publishing the destination; potentially throwing coercions are not folded,
duplicated or moved. Return and throw preserve the supplied value, including
object identity. The pinned interpreter's RETURN, RETURNUNDEFINED, NOP and
THROW_PREF_NONE handlers provide reference behavior. Function wrapper kinds
such as constructors/generators require additional semantics outside this API.

Two streaming passes validate then write without retaining source or a second
IR. Semantic, binding and budget failures produce no output; IO failures can
leave a prefix and must be handled by the caller's atomic file publication.
Inputs must stay immutable across passes. Work is O(instruction count plus
output bytes), with fixed scratch independent of instruction count.

Tests check output, exact/short budgets, Hole reads, terminal errors, rejected
exception regions, unsupported late instructions, register bounds, stream
failure with and without exceptions, and 200/201/4096/65536-instruction inputs.
A test-only wrapper executes emitted bodies in Node to verify numeric/string/
BigInt addition, throwing Symbol conversion, object coercion order/count and
thrown object identity. The wrapper is not recovered application code. Ark
execution comparison, historical semantic equivalence and per-record source
generation remain unverified.
