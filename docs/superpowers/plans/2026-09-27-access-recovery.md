# Access Recovery Implementation Plan

Execution: independent inline work, no CC or subagents. Approved design:
[access-recovery.md](../../design/source-recovery/access-recovery.md).

Goal: implement L0–L9 without replacing the T00–T12 gates. C++20, existing
typed IR, shared FunctionTree and printer; tests remain outside production.

## First Delivery: Computed Set (L1)

- [x] Add a failing narrow/wide STOBJBYVALUE import fixture in
  `tests/unit/analysis/value_import_test.cc`. Require three ordered operands,
  no accumulator result, retained write/throw effects and exact original PC.
  Run `cmake --build build --target value_import_test -j 2`, then
  `ctest --test-dir build -R '^value_import_test$' --output-on-failure`.
- [x] Add `ValuePropertyWrite` in `value_ir.h`; map `ValueWrite` in
  `value_import.cc` and base/key/stored-value in `value_operands.cc`.
  Mark nonerasable in `value_edits.cc`.
- [x] In `evaluation_shape.cc`, define three fixed roles. In planner/verifier,
  require stored-value producers to stay materialized: JS reference/key
  conversion precedes RHS syntax, whereas the VM has already computed RHS.
  Keep the existing conservative effect snapshot reason. Test a forged RHS
  inline plan, including a pure parent containing an effectful child.
- [x] Extend bounded expression operands to three; emit existing
  `AssignmentExpr{MemberTarget{base, ComputedKey{key}}, rhs}`. Independently
  verify all three operands in `tree_operands.cc`; no new writer.
- [x] Add full import/tree/print fixture and executable JS oracle covering
  key coercion vs RHS order, setters, throws, Symbol keys and preserved acc.
  Run only relevant unit/integration tests, not performance/RSS workloads.
- [x] Inspect actual ABC output where available, update status with exact
  evidence and remaining gaps, commit the coherent change.

Evidence: initial import test aborted at the missing ValueProgram assertion;
after implementation, value_import_test, value_operands_test,
value_simplify_test, evaluation_plan_test, function_tree_test and
computed_store_test pass (6/6). Production abc_semantic builds. Existing
pasteboard 7449 and Immersive 21082 drafts remain 1434/160 bytes and incomplete;
these are regression observations, not new computed-store corpus coverage.

## Subsequent Deliveries

L0 typed coverage catalog follows existing ISA generation/profile ownership;
include stage-specific state rather than guessing support from source text.
L2 retains fixed-index/runtime differences and requires receiver/path proof.
L3 closes module initialization/live bindings and global target semantics.
L4 implements cross-creation environment/capture access summaries.
L5 integrates define/receiver/private through ordinary callable ownership.
L6 and L7 implement exception phases and certified generator/async owners.
L8 handles protocol-specific access, L9 record and cross-application acceptance.
Each uses the A01–A20 matrix and combination cases in the approved design;
write bounded per-delivery steps against actual code before implementation.
No later delivery is marked implemented by this plan or by L1 passing.
