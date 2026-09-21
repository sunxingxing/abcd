# Dynamic Constant References

Evidence inspected against OpenHarmony-v7.0-Release: runtime_core
`3e147bb76e0b622cc2e4399ef954b030c0639691` and ets_runtime
`f04900cf951c66c2ea18b2bab5b591d5336c34b9`. Both use Apache-2.0.
Paths below are relative to those repositories. This is a reference audit,
not a claim that source recovery or literal payload interpretation is complete.

## Regional Indexes Are Not Entity Types

In runtime_core `libpandafile/file.h`, `ResolveOffsetByIndex` and
`ResolveMethodIndex` both read `GetMethodIndex`. In ets_runtime
`ecmascript/jspandafile/program_object.h`, `ConstantPool::GetEntityId` likewise
reads the method index belonging to its stored index header. The callers
include method materialization and object/array literal materialization.
Therefore the table name does not establish the type of every indexed entity.

Resolve a pool index in the enclosing method's region, retaining the typed
operand role separately from its resolved entity offset. Do not use one global
pool, infer a method from membership in method_idx, or treat an index as a file
offset. Regional class/field/proto tables remain distinct. Different regions
can map the same numeric index to different entities.

The metadata index validates, sorts and retains regional headers within its
existing memory budget. Its private Resolve function handles class/proto
references. ResolvePoolReference reuses the retained headers, using binary
region lookup followed by a four-byte positional table read: O(log R) lookup
for R regions, without loading every referenced entity or scanning the table.
PoolIndex is distinct from EntityOffset; PoolReference retains the requested
method/string/literal-array role. A resolved offset is not a payload-type
certification. Callers must supply the unmodified index for the same open file
and header. File identity is checked before and after successful resolution.
These are existing stat-based checks, not a guarantee against same-size writes
with unchanged observed timestamps. Inputs must remain immutable while read;
the corpus driver additionally verifies their content hash before inspection.

The initial resolver unit fixture covers mixed roles, equal indexes in adjacent
regions, half-open boundaries, invalid target offsets, index overflow, static
profile rejection and deterministic file-size mutation. Debug CTest passed
19/19 and ASan/UBSan CTest passed 14/14 after integration. These tests do not
yet establish full-corpus reference coverage or payload semantics.

## Literal Payloads Use a Different Coordinate System

In runtime_core `libpandafile/literal_data_accessor-inl.h`, an array starts
with a U32 count; the ordinary tagged loop increments that count by two per
tag/value pair. Payload widths come from LiteralTag, not a uniform word width.
The accessor has separate overloads for a global literal-array index and a
direct EntityId. The index overload first consults the global literal table.
Do not apply that extra indirection to an already resolved pool entity.

In ets_runtime `ecmascript/jspandafile/literal_data_extractor.cpp`, STRING
payloads go directly into EntityId and GetStringData. METHOD, GETTER, SETTER,
GENERATOR and ASYNC_GENERATOR entries retain a method offset and function kind;
the following METHODAFFILIATE supplies the function length used to construct
the function. These entries cannot each become independent JS array elements.
Object extraction also separates indexed properties from other properties.
Preserve ordered typed entries until the consuming opcode's literal semantics
are known; do not flatten a literal buffer directly into an object expression.

The generic accessor's FLOAT case numerically converts a U32 to float, unlike
DOUBLE which bit-casts a U64. The examined dynamic object extractors have no
FLOAT case and reject unhandled tags. A decoder must preserve raw payload bits
and report unsupported consumer semantics rather than silently assume IEEE
float interpretation from the tag name. Likewise, packed ARRAY_* cases in the
generic accessor terminate tagged enumeration and expose a payload offset;
their existence alone does not establish dynamic-language support or classify
the entire ABC file as static.

## Historical Boundary

JSPandaFile constructors in `ecmascript/jspandafile/js_pandafile.cpp` select
IsNewVersion using a lexicographic version comparison against OLD_VERSION,
defined in js_pandafile.h as {0, 0, 0, 2}. The currently recognized dynamic
profiles beginning with 9, 11, 12, 13 and 24 all select the new-format path in
this pinned runtime. They must not be routed to the old single-pool path merely
because their major version predates 24.

`PandaFileTranslator::GenerateProgram` in panda_file_translator.cpp explicitly
uses a separate constant-pool construction path for the older format. Its
existence does not prove that the current instruction decoder supports that
format. Keep container, ISA and semantic support independent and reject
unverified combinations explicitly.

## Implementation Checks

ResolveMethodReferences connects decoded instruction operands to regional
resolution. It counts reference operands within a capacity budget, sorts by
typed role, pool slot and byte PC, then deduplicates before resolving each
unique role/slot pair. The result retains the earliest use and direct target
offset for later lazy payload loading. Equal slots with distinct roles never
merge. Register/immediate operands are ignored; unsupported type/field roles,
oversized pool indexes and malformed operand counts fail explicitly.

For I instructions, K reference operands, U unique pairs and R regions, time is
O(I + K log K + U log R), with O(K) admitted vector capacity. Erasing duplicate
entries does not pretend to release the original capacity. This function
requires the index/header/body from the same immutable file. It validates
entity ranges only, not target payloads, method membership, nested literals
or source-level bindings. The abc_references batch tool additionally reads
direct string and literal-array targets, but does not recursively inspect
nested targets or certify method membership. Counts are per-method aggregates;
failed methods may contribute partial successfully checked payload counts.
Resolver regression coverage includes duplicate uses, distinct roles sharing
one slot, retained first-use PCs, overflow and invalid-target diagnostics,
malformed operand counts, unsupported roles, zero-budget empty input and
200/201/1024/8192 instruction collections. Debug CTest 23/23 and ASan/UBSan
CTest 18/18 passed after this integration.

ReadStringEntity now accepts a direct EntityOffset and returns UTF-16 code
units. It reuses the metadata MUTF-8 validator, then converts validated bytes
in a second linear pass. Encoded NUL, individual surrogate units and canonical
four-byte supplementary scalars are preserved without normalization. Raw
bytes and UTF-16 capacity (including its terminator) share a per-call budget;
allocation failure is a typed resource error. This is a synchronous bounded
reader, not the asynchronous function decompilation service. Source escaping
and choosing identifier versus string-literal syntax remain separate work.
String tests cover ASCII, two/three/four-byte sequences, encoded NUL, lone
surrogates, equivalent surrogate-pair encodings, malformed/truncated input,
budgets and lengths 200/201/1024/8192. After this addition, Debug CTest passed
20/20 and ASan/UBSan CTest passed 15/15. Full-corpus string reference validation
has not yet been run.

- Test two regions sharing an index but targeting different entities; include
  gaps, adjacent boundaries, index overflow and absent-table sentinels.
- Test one shared pool table containing method, string and literal references;
  preserve typed operand roles and reject invalid target ranges.
- Distinguish global literal indexes, pool indexes and direct entity offsets
  with strong types. Resolve nested references lazily, with cycle handling.
- Preserve MUTF-8 encoded NUL and UTF-16 surrogate units when producing JS
  strings; byte preservation alone is not valid JS source generation.
- Charge retained regional indexes, string buffers and literal payloads to
  bounded admission. Never expand all literal dependencies eagerly.
- Validate real corpus references after unit fixtures, but do not label
  structural reference coverage as source-semantic equivalence.
