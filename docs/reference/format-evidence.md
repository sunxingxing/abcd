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

These are reference-source observations, not corpus classification results.
The corpus classifier must verify header structure and record evidence before
persisting a static exclusion. Unknown or malformed inputs remain distinct.
