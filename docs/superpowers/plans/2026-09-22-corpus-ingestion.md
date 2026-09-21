# Corpus Ingestion Implementation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development, with
> tests before implementation and spec then quality review.

**Goal:** Reliably classify the complete corpus, persist static exclusions and
expose validated dynamic headers for subsequent lazy record/method decoding.
This is input infrastructure, not completion of the source decompiler.

**Architecture:** Allocation-free header parsing consumes a 60-byte prefix plus
the actual file size. Typed profiles separate known dynamic, known static and
unknown versions; malformed data is a distinct error. A streaming scanner hashes
each file, reuses classifications by SHA-256 and detector revision, and atomically
writes a locked JSON manifest. Unknown old formats need positive language evidence.

**Tech Stack:** C++20/CMake/CTest, OpenSSL EVP SHA-256, pinned local nlohmann/json.

## Task 1: Typed Header

Files: `include/abcd/abc/header.h`, `src/abc/header.cc`,
`tests/unit/abc/header_test.cc`, `CMakeLists.txt`.

- [ ] Declare `AbcVersion` (four bytes), `AbcKind` (dynamic/static/unknown),
  `HeaderError`, `Header`, and `ParseHeader(span, uint64_t actual_size)` returning
  `variant<Header, HeaderError>`. Store named fields for every header word and
  preserve numeric versions. A valid header is not a valid method or file body.
- [ ] Write always-active tests first: bad magic; short prefix; mismatched file
  size; size over uint32; foreign range overflow; misaligned/out-of-file index;
  count overflow; zero-count empty indexes; dynamic 9/11.0.2/12.0.2/12.0.6/13.0.1/24;
  static 0.1.0.7; unknown 0.0.0.6 and future versions. Incompatible 11.0.0/11.0.1
  must not acquire a supported profile. Test post-12.0.6 reserved literal fields.
- [ ] Verify missing implementation fails with `cmake --build build/ingestion`.
- [ ] Parse little endian through ByteReader, not struct reinterpret casts.
  Bounds use checked subtraction/division. Index widths: 4-byte offsets and
  40-byte region headers. Unknown layouts are not validated as a known profile.
  Current static and old dynamic profiles use literal indexes; new dynamic
  headers reserve both literal words. Cite pinned upstream constants.
- [ ] Build and run `ctest --test-dir build/ingestion --output-on-failure`,
  repeat sanitized build, review and commit.

## Task 2: Streaming Corpus Manifest

Files: `include/abcd/corpus/scan.h`, `src/corpus/scan.cc`,
`src/corpus/manifest.cc`, `tools/abc_scan.cc`,
`tests/unit/corpus/scan_test.cc`, `tests/corpus/abc-index.json`, CMake configuration.

- [ ] Use fixed-size streaming buffers; typed hash/detection evidence, SHA-256
  key, file size, numeric version, kind, revision and checked_at. No whole-file
  loading for hashing. Parse JSON at serialization boundary only. File paths and
  textual labels are allowed boundary data, not internal semantic dispatch.
- [ ] Tests first: initial scan, exact-content cache reuse, duplicate contents,
  changed contents at the same path, detector revision invalidation, corrupt
  manifest rejection, unknown and malformed inputs distinct from static,
  filesystem failure, lock contention, and interrupted-write preservation.
- [ ] Add explicit old-static language evidence only after reviewing the pinned
  static and dynamic formats. Never infer static from directory, size or failed
  dynamic decoding. Persist the exact observed offset and value for each rule.
- [ ] Lock a sibling lock file, bound manifest input size, and write to a unique
  sibling temporary file; flush and rename atomically. Never modify corpus ABCs.
  Preserve entries from other corpus roots when updating one root.
- [ ] Run the actual 900-file corpus twice; second run must rehash bytes but
  reuse classification. Persist classifications, report every category separately,
  and record peak RSS under 1,000,000,000 bytes. This measurement covers scanning
  only, not future full decompilation. Review and commit.

## Remaining Objective

Lazy record/method indexes, profile-specific ISA decoding, bounded asynchronous
method tasks, semantic IR, source generation, one output per record and behavior
verification remain required. No assembly, sidecar copying or placeholders may
be presented as successful source output.
