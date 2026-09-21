# Bundle Record Discovery

Reference: the pinned OpenHarmony-v7.0-Release runtime's
`JSPandaFile::InitializeUnMergedPF`, its constructor, and runtime_core's
`ClassDataAccessor::EnumerateFields`. This is not independent proof of all
historical runtime semantics.

`ReadBundleRecordAttributes` discovers the single runtime entry record from
local classes in file order. Its caller must establish bundle packaging and
provide the exact descriptor passed to the runtime constructor. A host corpus
path is not evidence of that descriptor.

- `L_ESModuleRecord;` supplies field values while the module remains absent.
  The outer condition is evaluated once per class, not once per field.
- Every field whose name differs from the constructor descriptor overwrites
  the module value; the callback's return does not stop field enumeration.
- The pinned comparison takes utf16_length bytes of the raw MUTF-8 name.
  Unicode normalization or decoded-string comparison would change behavior.
- UINT32_MAX clears the module pointer. Later classes may then supply it.
- `L_CommonJsRecord;`, `isSharedModule`, and `L_HasTopLevelAwait;` supply the
  respective flags. The shared marker intentionally has no descriptor wrapper.
- The reader rejects invalid selected references and missing values rather
  than pretending they represent empty modules. Strings are read individually
  under an allocation budget, with typed allocation failures.

Complexity is linear in records and inspected field-name bytes, plus logarithmic
regional lookup for word-encoded fields. No method bodies are loaded.

Unit coverage includes overwrite order, descriptor exclusion even for a missing
value, sentinel reset, later-class selection, early selected-class retention,
flags, unsupported profiles, bad names and owner mismatch. The corpus CLI now
uses this reader for bundle files. When a module class carries fields, it
requires `--bundle-descriptor VALUE` before MANIFEST; the value applies to all
bundle files selected by that invocation. Without module fields, no descriptor
can affect discovery, so no option is required. Descriptor provenance remains
the caller's responsibility. Integration tests exercise missing, valid and
invalid descriptor selections as well as descriptor-independent bundles.
