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
The dynamic runtime has its own module accessor; its ordered entry tables are
now implemented by ReadModuleInfo, as documented in module-layout.md.
Do not feed moduleRecordIdx to ReadLiteralArray
merely because the old implementation locates it through a literal index.

## Merged Attributes

ReadMergedRecordAttributes now maps the six runtime names at the UTF-16 input
boundary into typed attributes. Boolean presence is distinct from false;
nonzero values convert to true. Recognized duplicate fields overwrite in file
order. A module value of U32_MAX means absent and a request-phase value of zero
means absent. Other pointer values are range-checked. JSON presence retains a
string entity offset, not eagerly parsed JSON or source text.

For VALUE encoding, the field's type index resolves through the enclosing
region's class table. F64/I64/U64 encodings are rejected as incompatible with
the runtime consumer's U32 access; their inline word is not misread as a module
pointer. INT_VALUE preserves its direct signed-LEB bits independently. Missing
recognized values, invalid field slices/owners/names and invalid type references
return typed failures. Field names are read one at a time under a StringBudget;
no map of semantic strings or eagerly loaded field-name catalog is created.

This API is specifically for merged records. DetectFilePackaging now mirrors
CheckIsBundlePack in the pinned JSPandaFile, which changes to merged mode
when any local field is named isCommonjs or moduleRecordIdx. It does not test
the field's boolean value, and jsonFileContent alone is not this packaging
witness. The detector scans indexed local-field names with only one bounded
name buffer live and stops at the first witness. It does not require a value
or attempt to interpret malformed values during classification. Empty local
field sets are bundle packaging, not evidence that the ABC is static.
Unmerged special-class rules must use a separate path. Package-name,
npm-entry and scope-name handling also remain separate work.

For F fields, W recognized word-valued fields, R regions and total decoded name
bytes N, the pass costs O(F + N + W log R), with only one name buffer live. It
does not yet resolve imports/exports, validate module payloads across the corpus
or produce record source files.

Attribute tests cover all six recognized fields, false versus absence, nonzero
booleans, pointer sentinels/ranges, duplicate overwrite order, missing values,
inline word types versus all three wide types, type-index bounds, name budget
and field-slice bounds. Debug CTest passed 26/26 and ASan/UBSan passed 20/20.
Full-corpus attribute/module discovery has not yet been run.

Packaging regressions cover false-valued CommonJS presence, valueless module
presence, JSON-only and empty-field bundle classification, unsupported profiles
and name budgets. After detector integration, Debug CTest again passed 26/26
and ASan/UBSan passed 20/20.
