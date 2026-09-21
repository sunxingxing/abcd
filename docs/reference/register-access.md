# Register Access Windows

`GetRegisterAccesses` supplies the register component of instruction data flow
for later SSA and expression recovery. It accepts a decoded instruction and
the frame size (local registers plus arguments), without allocating memory.
Method decoding uses it to reject ranges extending beyond the frame.

Source: pinned OpenHarmony-v7.0-Release `isa.yaml`,
`ecmascript/interpreter/interpreter-inl.cpp` (`CALL_INITIALIZE`,
`DEPRECATED_CALL_INITIALIZE`, argument-pushing macros and opcode handlers), and
`ecmascript/stubs/runtime_stubs-inl.h::RuntimeCreateObjectWithExcludedKeys`.

The last immediate supplies the range count; the last explicit register supplies
its base. For these pinned encodings:

| Family | Registers potentially read from base |
| --- | --- |
| callrange, wide.callrange | count |
| supercallthisrange, supercallarrowrange, wide forms | count |
| callthisrange, callthisrangewithname, wide forms | count + 1 (receiver) |
| deprecated.callrange | count + 1 (callee) |
| deprecated.callthisrange | count + 1 (callee; encoded count includes receiver) |
| newobjrange, both IC widths and wide form | count (includes constructor) |
| createobjectwithexcludedkeys, wide form | count + 1 (keys); object is separate |

New-object and deprecated-this calls require a nonzero count. Empty ordinary
argument ranges do not read their encoded base. The excluded-key range is a
may-read set: an undefined first key suppresses later key reads at runtime.
It must not be converted into eager source evaluation without semantic lowering.

The ISA range flags alone are insufficient: narrow callthisrange has range_0,
yet the interpreter also reads the receiver; deprecated.callrange also reads a
callee omitted by range_0. The typed opcode mapping deliberately corrects these
cases rather than dispatching on printed mnemonic strings.

Explicit register accesses retain operand order and access direction; overlapping
windows are not duplicated into vectors or normalized as execution order.
Accumulator flags currently preserve the ISA declaration. This is not a complete
effect model: hidden function/new-target/argument/environment state, heap effects,
abrupt completion and precise exceptional-edge definitions remain separate work.
Writes must not be assumed to occur on an exceptional exit.

Work and storage are bounded by the maximum encoded operand count, independent
of range length. The method pass stays linear in instructions and adds no
retained per-instruction allocation. Tests cover all 17 range encodings, explicit
read/write operands, zero/invalid counts, endpoint overflow, and counts 200, 201,
1024 and 65535. Historical profiles accept these pinned rules, but independent
historical runtime semantic equivalence is not established by these tests.
