# Dynamic Record Index Implementation Plan

> **For agentic workers:** Use subagent-driven-development for implementation,
> followed by independent specification and quality reviews.

**Goal:** Index the real dynamic corpus into owned records and methods with
validated, lazy code locations. This is a prerequisite for instruction decoding,
not completion of the source decompiler.

**Architecture:** Reuse positional AbcFile IO and typed container profiles.
Parse local inline class members; resolve regional references through sorted
nonoverlapping intervals. Keep method instructions on disk. Bound metadata
growth and report typed malformed-input/resource errors.

**Tech Stack:** C++20, CMake/CTest, existing OpenSSL/zlib fingerprinting and JSON
manifest boundary. Reference evidence: docs/reference/record-layout.md.

## 1. Metadata Index

- [ ] Add typed record, method, code-location and index-error APIs under
  include/abcd/abc; implement bounded parsing under src/abc.
- [ ] Add synthetic unit tests under tests/unit/abc before implementation:
  valid local/foreign classes, encoded MUTF-8 lengths, signed field constants,
  repeated annotations, malformed tags, truncated references, wrong ownership,
  overlapping regions, code bounds and metadata budget exhaustion.
- [ ] Use lower_bound/upper_bound regional lookup, never per-method linear
  searches over all regions. Test 200/201-sized indexes.
- [ ] Register libraries/tests in CMakeLists.txt; split file IO dependency from
  corpus JSON logic if needed, retaining existing APIs.
- [ ] Run Debug/Release tests and sanitizer library tests; inspect source and
  memory behavior. Commit coherent verified implementation.

## 2. Real-Corpus Validation Driver

- [ ] Add tools/abc_index.cc: consume persisted corpus manifest and explicit
  root mapping, verify content hashes, skip proven static inputs without
  redetection, index each dynamic file independently.
- [ ] Establish process address-space ceiling <=1,000,000,000 bytes before
  loading metadata. Report peak RSS, file/record/method/code counts and typed
  per-file failures; do not label index success as decompilation success.
- [ ] Add integration tests for invalid arguments, changed content and a
  repeatable small valid corpus. Keep tool outputs outside source directories.
- [ ] Run all 402 dynamic paths from paired and batch roots; preserve diagnostic
  evidence for every failure, fix supported metadata gaps rather than relabeling
  those files static. Record counts and measured memory in docs/validation.

## 3. Review and Next Stage

- [ ] Independent specification review, then quality review; fix findings and
  rerun affected tests. Commit validation and keep the worktree clean.
- [ ] Proceed to version-aware typed instruction decoding and semantic recovery.
  Full objective still requires correct JS/TS/ArkTS, one file per record,
  asynchronous bounded function tasks and behavior-based corpus verification.
