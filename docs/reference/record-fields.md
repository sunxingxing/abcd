# Record Fields and Module Metadata

Pinned sources: runtime_core `3e147bb76e0b622cc2e4399ef954b030c0639691`,
ets_runtime `f04900cf951c66c2ea18b2bab5b591d5336c34b9`,
OpenHarmony-v7.0-Release, Apache-2.0.

## Retained Representation

BuildMetadataIndex retains each local record's contiguous FieldInfo slice,
identified by first_field and field_count. Each field stores its own offset,
validated owner, name entity offset, regional type index, access flags and an
optional encoded value. Field names remain lazy string entities. Annotation
ordering/ranges and duplicate-value validation remain unchanged.

INT_VALUE is signed LEB32. The reader preserves its sign-extended two's-
complement U32 bits without signed overflow. VALUE retains the inline U32 word.
In runtime_core libpandafile/field_data_accessor.cpp, VALUE is an inline F32
word for F32 fields but an offset to a U64 payload for I64/U64/F64 fields.
Consequently neither encoding alone establishes a source-level numeric value.
Field type resolution and wide-value dereferencing remain separate work.

Retained field vector capacity, including old/new buffers during growth, is
charged to MetadataBudget. Enumeration is linear in fields; no per-record
scan through all fields is needed. Index output reports aggregate field counts.

## Runtime Consumers

ets_runtime ecmascript/jspandafile/js_pandafile.h defines the external field
names isCommonjs, jsonFileContent, moduleRecordIdx, isSharedModule,
hasTopLevelAwait and moduleRequestPhaseIdx. InitializeMergedPF in
js_pandafile.cpp reads these as boolean, U32 or I32 values into typed record
fields. It publishes a JSRecordInfo only when the CommonJS or JSON field is
present. The unmerged path is different and must not inherit this rule blindly.
These names belong at a validated decoding boundary, not as internal semantic
string tags or string-keyed state.

ecmascript/module/module_data_extractor.cpp treats moduleRecordIdx as a direct
module entity offset for IsNewVersion, but as a global literal-array index on
the older path. A module payload is not an ordinary tagged literal array:
libpandafile/module_data_accessor.cpp skips a leading word, then reads the
module-request count and U32 string references before the module-entry tables.
The dynamic runtime has its own module accessor, which must be inspected before
implementing those entry tables. Do not feed moduleRecordIdx to ReadLiteralArray
merely because the old implementation locates it through a literal index.

This change retains the evidence needed for module parsing. It does not yet
classify record fields, resolve imports/exports or produce record source files.
