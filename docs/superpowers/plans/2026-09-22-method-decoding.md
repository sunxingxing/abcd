# Typed Method Decoding Implementation Plan

> **For agentic workers:** Use subagent-driven-development with independent
> specification and quality reviews. Existing feature branch is isolated and
> clean; no additional worktree is needed.

**Goal:** Decode actual dynamic method instructions and exception tables into
typed C++ structures usable by semantic recovery, without assembly output.

**Architecture:** Generate encoding descriptors from the pinned upstream ISA
and its established Ruby API. A bounded single-instruction primitive feeds
owned per-method instruction/exception arrays. Packed instruction-boundary
bits validate jump and handler targets without repeated instruction scans.

**Tech Stack:** C++20, CMake/CTest, pinned runtime_core ISA inputs, Ruby/ERB at
build time only, existing positional AbcFile IO and corpus manifest.

## 1. Generated Instruction Decoder

- [x] Add include/abcd/abc/instruction.h, src/abc/instruction.cc and a custom
  ISA template/generation CMake module. Verify hashes of generator inputs;
  write generated enum/descriptor headers under the build directory.
- [x] Use typed opcode, operand kind/access, widths, raw numeric bits and
  branch displacement. No runtime dispatch on textual mnemonic or format.
- [x] Before implementation, add tests/unit/abc/instruction_test.cc covering
  all prefix families, nibble registers, signed branches, float bits, invalid
  opcodes, each truncated encoding and unsupported profiles.
- [x] Keep source-format generation strings at that boundary only. Preserve
  deprecated encodings for old profiles; do not claim semantic certification
  from a successful encoding lookup.
- [x] Verify tests and commit the decoder independently.

## 2. Method Bodies and Exceptions

- [x] Add include/abcd/abc/method_body.h and src/abc/method_body.cc, with tests
  in tests/unit/abc/method_body_test.cc. Read one requested code item, compare
  its prefix with indexed metadata and preserve exact instruction offsets.
- [x] Check explicit registers against registers+arguments using widened
  arithmetic. Check signed branch destinations for range and instruction
  boundary; never silently round a target to an instruction.
- [x] Decode try(start,length,catch_count) and catch(type+1,pc,length) ULEBs.
  Preserve try/catch order and typed catch-all absence. Validate half-open
  ranges and handler entries; permit overlapping/nested try regions.
- [x] Bound code bytes, packed boundaries, vector capacities and reallocations
  within a per-method allowance. Return typed failures and retain file-change
  detection. No eager whole-file code loading.
- [x] Test valid control flow/handlers, negative and interior jumps, overflow,
  truncated exception LEBs, invalid registers, empty/oversized code and budgets.
  Include 200/201 instruction/handler cases.
- [ ] This synchronous decoding primitive is not the default asynchronous
  decompilation service. Integrate bounded lazy task scheduling before exposing
  the user-facing function decompiler; never use unbounded std::async.

## 3. Corpus Driver and Verification

- [x] Extend the existing manifest-backed index validation workflow with a
  distinct abc_decode CLI. Reuse hash-verified static exclusions and root-confined
  opening; keep successful index counts distinct from decoded-method counts.
- [x] Enforce the same 1,000,000,000-byte process ceiling, retain at most one
  method body at a time, and report typed per-method failures with offsets.
- [x] Run all 402 dynamic paths and all indexed methods, not just paired
  examples. Record instruction/exception counts, failures, timings and RSS.
- [x] Run Debug/Release and ASan/UBSan tests; review spec then quality, fix
  findings, commit evidence and keep the worktree clean.

## Remaining Objective

Encoding/exception coverage is not semantic equivalence. Continue through
reference/literal/module resolution, CFG/SSA/effect/closure/async recovery,
bounded function tasks, and JS/TS/ArkTS generation into one file per record.
No placeholder bodies, sidecar-source copying or assembly satisfy this goal.
