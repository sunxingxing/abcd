# P2 Iterator Contract Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the v7 iterator instruction family a validated, typed P2 semantic contract before attempting source recovery.

**Architecture:** Keep iterator acquisition, closing and legacy next as distinct variant alternatives. Decode their exact operand widths into typed fields, then reuse that lift in register-effect analysis. This task does not emit `for...in` or `for...of`: loop shape, close-on-abrupt behavior and exception state must first be proved at function scope.

**Tech Stack:** C++20, generated v7 ISA table, CMake/CTest.

---

### Task 1: Typed iterator operations

**Files:** `include/abcd/analysis/iterator.h`, `src/analysis/iterator.cc`, `tests/unit/analysis/iterator_test.cc`, `CMakeLists.txt`.

- [x] Write a decoder test for all seven encodings: `GETPROPITERATOR`, narrow/wide `GETITERATOR`, narrow/wide `CLOSEITERATOR`, `GETASYNCITERATOR`, and deprecated `GETITERATORNEXT`. Assert the variant, IC slot, registers, operand-width errors and frame bounds.
- [x] Run `cmake --build build -j 4 --target iterator_test`; it failed before the implementation existed.
- [x] Implement typed `IteratorInstruction` alternatives, with `IteratorMode` distinguishing sync and async acquisition. Return typed unsupported/malformed/register-range errors. Every successful operation may throw under the pinned v7 interpreter, regardless of the ISA group's `x_none` annotation.
- [x] Run `ctest --test-dir build -R '^iterator_test$' --output-on-failure`.

### Task 2: Analysis integration

**Files:** `src/analysis/register_effect.cc`, `tests/unit/analysis/register_effect_test.cc`.

- [x] Add failing assertions: acquisition reads and writes acc; close reads its iterator register and writes acc without reading the old acc; legacy next reads two registers and writes acc. Verify malformed operand failures survive dispatch.
- [x] Reuse `LiftIteratorInstruction` in `LiftRegisterEffect`; do not derive these effects from generic ISA read/write flags. Run `register_effect_test`, `register_ssa_test` and `register_value_flow_test`.
- [x] Run the full CTest suite and complete-corpus probe; inspect `git diff --check` and commit.

### Task 3: Private-property operations in the same ISA group

**Files:** `include/abcd/analysis/private_property.h`, `src/analysis/private_property.cc`, `tests/unit/analysis/private_property_test.cc`, `src/analysis/register_effect.cc`, `tests/unit/analysis/register_effect_test.cc`, `CMakeLists.txt`.

- [x] Verify LDPRIVATEPROPERTY, STPRIVATEPROPERTY and TESTIN operand roles against the pinned interpreter. Add failing tests for level/slot widths, receiver register bounds and accumulator effects.
- [x] Lift a typed private operation with lexical level/slot and IC evidence; connect its normal-completion register effects. Keep source emission refused until class/private-name identity is proved.
- [x] Run private-property, register-effect, SSA and value-flow tests.

### Function-to-record continuation

After this contract, select a real iterator-bearing paired method and preserve its complete normal and abrupt iterator-close paths in the function IR. Add a positive runtime oracle and a near-miss refusal before admitting `for...in` or `for...of` source. Only then may a record referencing that method move from structural output to verified `.mjs`. The full-function/record admission and 1G RSS checks are separate evidence gates; a typed lift alone does not pass them.
