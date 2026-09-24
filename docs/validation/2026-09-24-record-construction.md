# Record Entry Constructor Calls

Reference: pinned `OpenHarmony-v7.0-Release` identities in
`docs/reference/openharmony-v7.0-release.lock.json` and
`docs/design/constructor-calls.md`.

The record entry interpreter now admits validated `NEWOBJRANGE` forms for
dynamic 12.0.6 and 13.0.1.0. The typed range includes the constructor;
subsequent registers are argument snapshots. Source uses native `new` with
the same constructor/new-target identity. Constructed values receive typed
instance identities and may flow through registers into module exports.
Unsupported value origins still refuse the record rather than inventing JS.

For a class whose source emission was deferred, admission requires no
intervening effectful entry instruction. The barrier is maintained in O(1)
per instruction, not rescanned for every constructor. Class fields and
accessors that still need later attachment block this route. Literals and
numeric atoms as constructor arguments remain unimplemented.

A file-backed record fixture exports a constructed function instance. C++
checks the emitted constructor call and refusal for an uninitialized argument;
Node executes the emitted ESM and checks its exported instance. The same
fixture passes under 12.0.6 and 13.0.1.0 profiles. The subsequent rest
integration passed 159/159 CTests.

The real 12.0.6 pasteboard `Logger` record (entity 4466) now emits source.
Its `debug/info/warn/error(...args)` members have source length zero, one ABI
user-argument slot and `COPYRESTARGS 0`. The rest-prologue proof recovers
their signatures, and the existing tail named-invocation path preserves the
early property read, receiver, and argument evaluation order across
intervening instructions. `record_constructor_source_test` runs the emitted
record with a hilog test module and checks the exported instance, fields,
method lengths, receiver identity, and all four call argument lists. This is
evidence for this record, not complete coverage of every CALLTHIS form.

The preceding full method-body probe processed 900 paths, 402 dynamic and
498 cached static, with zero file failures and 176,762,880-byte peak RSS.
It still admitted 1,500,536 of 5,486,254 method bodies. That run preceded
record-entry constructor integration and does not prove full record output
or the process-memory limit for every possible output path.
