# Iterator Operations: P2 Contract

The `iterator instructions` group in pinned OpenHarmony-v7.0-Release
`runtime_core/isa/isa.yaml` contains iterator, private-property and named-field
operations. The seven iterator encodings now lift to typed `PropertyIterator`,
`AcquireIterator`, `CloseIterator` or `IteratorNext`. The three private-property
encodings lift separately to `PrivateLoad`, `PrivateStore` and `PrivateHas`,
with IC evidence, lexical level/slot and a typed store receiver. `DEFINEFIELDBYNAME`
and `DEFINEPROPERTYBYNAME` already use the reviewed own-write/field paths.

The pinned `ets_runtime/ecmascript/interpreter/interpreter-inl.cpp` handlers at
GETPROPITERATOR, GETITERATOR, GETASYNCITERATOR, CLOSEITERATOR and deprecated
GETITERATORNEXT establish the inputs and normal results. Acquisition consumes
the accumulator and replaces it with an iterator; closing reads its explicit
iterator register, not the old accumulator, then replaces the accumulator;
legacy next reads iterator and method registers and replaces the accumulator.
Each handler can throw through the slow runtime. The group's ISA `x_none`
annotation must not be interpreted as a no-throw proof. IC immediates are
retained as encoding evidence, never source operands.

`LiftRegisterEffect` consumes this lift, so malformed width/kind/count and
out-of-frame register errors remain typed instead of falling back to generated
ISA flags. The lift is constant-time and allocates nothing. These facts do not
yet identify `for...in`, `for...of` or `for await...of` source: those require a
whole-function proof of iterator acquisition, next/close ordering, abrupt
completion and exception state. Until such a proof and runtime oracle exist,
the source writer keeps these instructions unsupported. A complete record
containing them is not published as executable source.

The pinned LDPRIVATEPROPERTY and TESTIN handlers read the receiver from acc
and replace acc on normal completion. STPRIVATEPROPERTY reads its receiver
register and acc value, then restores acc on normal completion. All may throw.
The level/slot pair locates a private name through the lexical environment; it
does not alone prove a source `#name`, its declaring class or field ownership.
Source emission remains refused until that identity and all relevant paths are
proved. The typed lifts and register effects do not change record admission.
