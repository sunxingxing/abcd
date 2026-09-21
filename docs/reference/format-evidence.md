# ABC Format Evidence

Reference: runtime_core commit `3e147bb76e0b622cc2e4399ef954b030c0639691`,
verified equal to peeled `OpenHarmony-v7.0-Release` on 2026-09-22.

- `libpandafile/file.h`, `File::Header`: 8-byte magic, 4-byte checksum,
  4-byte version, then eleven little-endian uint32 fields, total 60 bytes.
- `libpandafile/file.h`, `File::STATIC_VERSION`: **0.1.0.7**, not 0.0.0.7.
  `static_core/isa/isa.yaml` independently declares current version 0.1.0.7
  and minimum 0.0.0.6. A version range alone does not certify all historical
  static formats, particularly where old dynamic versions overlap.
- `isa/isa.yaml`: current dynamic version 24.0.0.0, minimum 0.0.0.2;
  incompatible versions 11.0.0.0 and 11.0.1.0 require explicit handling.
- `libabckit/src/abckit_impl.cpp`, `OpenAbc`: tries static reader then dynamic.
  Neither a failed reader nor this fallback order establishes classification
  evidence sufficient for our persistent skip manifest.
- `libpandafile/file.cpp`, `GetFileType`: checks minimum header length, declared
  file size and magic, then compares version exactly with `STATIC_VERSION`.
  Its catch-all dynamic branch is unsuitable for our historical/unknown-version
  classification: absence of the current static version is not positive proof
  that an arbitrary version belongs to the dynamic format.

These are reference-source observations, not corpus classification results.
The corpus classifier must verify header structure and record evidence before
persisting a static exclusion. Unknown or malformed inputs remain distinct.

## Historical Static Corpus

Subsequent read-only investigation of both corpus roots found 900 ABC files:
498 version 0.0.0.6, 2 version 9.0.0.0, 1 version 11.0.2.0,
35 version 12.0.2.0, 117 version 12.0.6.0, 247 version 13.0.1.0.
These counts are not decompilation results.

Static layout is **72 bytes**, per `static_core/libarkfile/file.h:59`:
quickened flag at 28, class count/index at 32/36, export count/index at 40/44,
LNP count/index at 48/52, literal count/index at 56/60, regions at 64/68.
Do not interpret these fields using the 60-byte dynamic layout.
Exports before version 0.0.0.7 are arrays of uint32 offsets; newer export counts
are byte counts (`file.h:205`, `METADATA_SINCE_VERSION` at line 497).

Positive static-language evidence is `ClassTag::SOURCE_LANG` (2), with value
ETS (5). Sources: `static_core/libarkfile/file_items.h:44`,
`static_core/plugins/ets/ets_plugin_options.yaml:22,28`,
`static_core/libarkfile/class_data_accessor.cpp:24`.
The class prefix contains ULEB128 encoded string length, terminated MUTF-8 name,
uint32 superclass, ULEB128 access flags/field count/method count, optional
INTERFACES tag (1) with ULEB128 count and uint16 indexes, then SOURCE_LANG.
The tag must be found by bounded structural parsing, never byte-pattern search.

The investigation found all 498 historical candidates have at least one local
class and every examined local class explicitly declared ETS. Their declared
sizes and Adler32 checksums also matched. Adler32 is seeded at 1 over bytes
from offset 12 to EOF (`static_core/libarkfile/file.cpp:563`). Largest historical
static candidate was 6,634,160 bytes. The production scan must reproduce and
persist per-content evidence before using these observations for exclusions.
Header/prefix/checksum validation is not a claim that all class/method bodies are
valid. A conservative rule may stop after the first structurally valid ETS witness.
