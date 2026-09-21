# Named Property Reads

`LiftNamedRead` models modern `LDOBJBYNAME_IMM8_ID16` and
`LDOBJBYNAME_IMM16_ID16` from the pinned `OpenHarmony-v7.0-Release`.
Both take the receiver from acc and publish the result to acc only after
successful completion. Their cache operands are not language operands.
`NamedRead::name` is a method-regional string-pool slot, not a file offset.

Evidence: `ecmascript/interpreter/interpreter-inl.cpp` handlers resolve the
constant string and invoke fast/slow named-property access. The slow path
`RuntimeLdObjByName` in `stubs/runtime_stubs-inl.h` invokes ordinary
`JSTaggedValue::GetProperty`; access is neither own-only nor pure. Getters,
prototype lookup, Proxy traps and errors remain observable.

The straight-line emitter renders `acc = (acc)["name"];` with the existing
UTF-16 string-literal writer. Bracket syntax avoids identifier restrictions
and preserves NUL, lone surrogates, quotes and backslashes in keys. No global
property-access helper or eager runtime property lookup is introduced.
String constants are read through the enclosing method's regional pool and
share the lazy constant-writing path with `lda.str`. Only one bounded string
is live. Existing full preflight, output budget and file-stability contracts
remain in force; source publication still requires the future atomic writer.

Lifting takes fixed time/storage. Each constant access adds O(log regions +
string bytes + output bytes) work, without caching all names or accumulating
body output. Accumulator reads before initialization are rejected.

Tests exercise both encodings, pool/cache boundaries, invalid operands,
missing context, malformed constant payloads with the originating PC,
exact/short output budgets, and no leaked prefix on failure. Generated
function bodies run in Node against unusual UTF-16 keys, inherited getters,
Proxy receiver/key identity, missing properties and null receivers. These
tests do not establish full Ark/historical execution equivalence.

## Legacy Constraint

The pinned `DEPRECATED_LDOBJBYNAME_PREF_ID32_V8` C++ handler assigns
`READ_INST_32_1()` to `uint16_t stringId`, narrowing the encoded field before
lookup. Historical/compiler execution paths need separate verification;
neither silently using the full encoded ID nor blindly applying this narrowing
to every profile is justified. The legacy semantic lift remains unsupported,
and a decoded-opcode assertion protects that regression test. Parsing old ABC
remains supported; complete legacy source recovery is still required.
