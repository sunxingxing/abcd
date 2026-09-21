# Dynamic Constant References

Evidence inspected against OpenHarmony-v7.0-Release: runtime_core
`3e147bb76e0b622cc2e4399ef954b030c0639691` and ets_runtime
`f04900cf951c66c2ea18b2bab5b591d5336c34b9`. Both use Apache-2.0.
Paths below are relative to those repositories. This is a reference audit,
not a claim that constant resolution or source recovery is implemented.

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

The existing metadata index already validates and sorts regional headers, but
discards them on return. Its private Resolve function handles class/proto
references only. Extract and reuse that bounded regional reader rather than
introducing a second interpretation of table bounds and absent-table sentinels.
Use binary region lookup followed by a positional table read: O(log R) lookup
for R regions, without loading every referenced entity or scanning the table.

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
