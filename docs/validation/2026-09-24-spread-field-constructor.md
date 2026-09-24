# Rest-Spread Derived Constructor Slice

The pinned OpenHarmony v7 interpreter decodes `COPYRESTARGS`,
`SUPERCALLSPREAD`, `THROW_IFSUPERNOTCORRECTCALL`, `LDA_STR` and
`STOBJBYNAME` for the dynamic profiles. The implementation uses the pinned
generated ISA table and source handlers as its format/semantic reference;
no sidecar TS file is read by the engine. The proof is a bounded linear
abstract interpretation, not opcode-name or printed-IR matching. Memory is
O(registers + named writes); constant strings are loaded one at a time and
source output is two-pass preflighted before publication.

The real pasteboard method entity 5083 proves one ordered string setter:
pool slot 56 is `curBp`, pool slot 31 is empty string. The emitted constructor
is `constructor(...$rest) { super(...$rest); this["curBp"] = ""; }`.
The instance write is intentionally not rendered as class field syntax:
`STOBJBYNAME` can invoke a prototype setter. The Node oracle exercises that
setter, rest arguments, constructor length and `new.target`-selected
prototype. Synthetic class fixtures cover 12.0.6.0 and 13.0.1.0; the real
sample integration test checks decode, proof, constant resolution and output.
Unit tests cover false rest/receiver provenance, control flow, resource
refusal and 200/201/400/800-instruction scaling inputs.

This is not a complete record result. The real pasteboard `EntryAbility`
constructor proves locally, but whole-class source still refuses its unknown
parent runtime invariant (`FunctionEmitError::kSuperclass`). A diagnostic run
before the invariant gate reached a further class-member refusal at
`EnvironmentFlowError::kExceptionRegions`. No incomplete `.mjs` is published.
This gate is necessary: pinned
`arkcompiler/ets_runtime/ecmascript/stubs/runtime_stubs-inl.h` checks
`IsJSFunction` in `RuntimeSuperCallSpread` before iterating rest args,
whereas its class-heritage path accepts a constructible Proxy parent; native
`super(...rest)` calls that Proxy. The constructor's prototype can also be
mutated to a Proxy after class creation. The ordinary-parent Node fixture
supplies the invariant for its restricted test execution; record import/global
provenance does not.

Fresh verification after the invariant gate: full Debug build and 161/161
CTest cases passed. `abc_structure` on pasteboard exited 0 with process peak
RSS 10,880 KiB (11,141,120 bytes) and left record 4767 without a source
path; debugger
inspection confirmed `FunctionEmitError::kSuperclass` at its class site.
The complete paired+batch `abc_source_probe` run exited 0 with peak RSS
173,948 KiB (178,122,752 bytes). It reused 498 cached static
classifications; its working index copy remained byte-identical to
`tests/corpus/abc-index.json`. The probe measures source-body admission, not
complete-record source equivalence or a full corpus output-memory proof.
