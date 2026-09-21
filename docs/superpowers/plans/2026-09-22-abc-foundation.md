# ABC Foundation Implementation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development or
> superpowers:executing-plans, with tests first and verified commits.

**Goal:** Begin the end-to-end dynamic ABC decompiler with a tested, bounded
binary reader. Final acceptance remains semantically faithful JS/TS/ArkTS,
one file per record for the complete dynamic corpus, within 1,000,000,000 bytes.

**Architecture:** Typed, bounds-checked binary access precedes profile-specific
decoding. Reference sources are pinned separately; source-paired corpus files
are evaluation oracles, never engine inputs. Use span views without owning
copies so future lazy readers can retain separately budgeted backing storage.

**Tech Stack:** C++20, CMake, CTest, standard library; external dependencies
selected and pinned only when required.

## Task 1: Binary Reader

Files: `CMakeLists.txt`, `include/abcd/abc/byte_reader.h`,
`src/abc/byte_reader.cc`, `tests/unit/abc/byte_reader_test.cc`.

- [ ] Define typed read failures and a span-backed reader for little-endian
  uint32, byte slices, and unsigned LEB128 uint32. Failure must not advance
  the cursor. Empty reads are permitted at EOF. Overflow/truncation are distinct.
- [ ] Write standalone CTest assertions that remain enabled in Release, covering
  byte order, empty/exact-end/oversized spans, truncated words, zero/max LEB128,
  fifth-byte overflow and continuation, truncated LEB128, and nonzero cursors.
  Example invariant: after a failed `ReadU32()`, `position()` is unchanged.
- [ ] Configure/build and record the expected missing-implementation failure:
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`, `cmake --build build`.
- [ ] Implement bounded operations using remaining-length subtraction (not
  unchecked offset addition), named format constants, typed results and no
  semantic strings. LEB128 reads at most five bytes; no allocation.
- [ ] Run `ctest --test-dir build --output-on-failure`; repeat in Release and
  ASan/UBSan builds, review specification then quality, and commit.

## Following Milestones

Separate concrete plans will follow evidence from the reader and reference code:
header/profile classification with a persistent static-sample manifest; lazy
record/method indexes; generated ISA decoding; asynchronous bounded method tasks;
exception-aware semantic IR; source recovery and per-record writing. They remain
required and are not implied complete by the foundation test suite.

The 1G gate includes all workers, cache, mapped pages, dependency allocations and
output buffers. Full-corpus peak RSS and child-process aggregate measurements
are required in addition to allocation accounting. Never substitute stubs,
assembly or copied sidecar source to pass coverage or memory checks.
