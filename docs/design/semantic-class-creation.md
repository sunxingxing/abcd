# Class creation in semantic IR

`ClassCreation` and `DefineAccessor` now enter the same `SemanticOperation`
model as ordinary function creation. The existing validated decoders retain
the constructor/literal pool slots, encoded length, cache slot, parent register
and accessor operand registers. `SemanticInstruction::pc` retains provenance;
no source-map input or name-based dispatch is involved.

## Runtime contract

Reference: pinned OpenHarmony-v7.0-Release `ets_runtime`:

- `interpreter/interpreter-inl.cpp`, class handlers at 5178-5220:
  read parent register and current frame environment/module; replace the
  accumulator only after non-abrupt class creation.
- `stubs/runtime_stubs-inl.h`, `RuntimeCreateClassWithBuffer` and
  `RuntimeSetClassInheritanceRelationship`: capture environment and module,
  allocate class/members, inspect heritage and potentially call a parent
  `prototype` getter. Hole means absent heritage; null means `extends null`.
- `RuntimeDefineGetterSetterByValue` at 2708: convert the key, optionally name
  accessor functions, update function kind and define a property. The input
  accumulator's ToBoolean controls naming, not enumerability. Enumerability
  depends on class ownership. Normal result is the object operand.

Both operations conservatively include heap reads/writes, user code and
allocation in normal/before-throw effects. Class creation additionally reads
environment/module state. Both may throw. Existing typed register-effect
lifting supplies all input uses and the normal accumulator definition to SSA.
Normal-result value rules remain Language, not a new object/heritage proof.
Decoded and semantic value-flow entry points agree on these rules.

## Proof boundaries

Class creation is a capture barrier in normal local-cell SSA and phased local
initialization. Transitive capture-preservation inspection refuses a nested
class until its constructor and literal-method dependency graph is resolved;
merely lifting a class must not certify a containing function as read-only.

This does not license native class syntax, accessor descriptors or treating
Hole as a source value. The generic input verifier still rejects unproven
internal values, and the source AST still refuses these operations without
ownership/heritage certificates. The existing record/class writer remains a
separate retained path. Completion requires those certificates, class AST
nodes and shared printing, including PC-to-generated-position propagation.

No new graph solver or per-variable traversal is introduced. Instruction
lifting is constant-sized; existing CFG/SSA work and memory budgets apply.
The new variant alternatives may affect structure size, which existing
sizeof-based admission accounts for. Tests and RSS measurement are paused;
no performance or source-equivalence claim follows from CLI compilation.

Deferred regressions: both class operand widths and register bounds, normal
versus exceptional accumulator state, throwing heritage/key conversions,
class capture barriers, nested class writers, accessor naming/enumerability,
Hole versus null heritage and fail-closed AST admission.

## Pasteboard checkpoint

ABC-only inspection of ProgressBar entry 9915 now builds the complete normal
semantic method: 156 instructions, 11 blocks and 157 SSA values, recorded in
`build/pasteboard-entry-class-ir.json`. Previously it rejected class creation
at PC264. The instruction inspection at
`build/pasteboard-entry-class-ir-instructions.jsonl` retains the class operands
and accessor definitions at PCs303, 326, 349 and 372 with exception edges.
Native source validation now reaches its independent local-environment gate
at PC130 (`SemanticProofError::kInternalValue`); it does not emit a class AST
or complete record. Both CLI targets built and read-only review found no
specific issue; no tests or RSS tracking were run.

### Initialization across the prelude join

The local initialization proof now accepts forward CFGs where environment
allocation dominates the first initializing store in a different block.
The store must still dominate every admitted subsequent read/capture, and
every store must pass Language-value verification. Cycles and closure-write
conditions are unchanged. Allocation-block lookup uses a quota-charged binary
search: O(S log B) additional work for S candidate stores and B blocks, with
no additional allocated storage or per-cell graph walk.

In entry 9915, debugger inspection at PC130 now reports `initialized=true`,
store instruction 47 (PC128), and dominance interval [4,10). The independent
unknown-closure-write check still rejects at PC130: creation instruction 32
has no capture-preservation premise at this standalone verifier entry. This
is an initialization-proof improvement, not a newly emitted method. Connect
actual child capture summaries next; do not substitute a blanket Language
assumption. CLI builds and read-only review completed; diamond/cycle/budget
regressions remain deferred while tests are paused.

### Creation-site preservation in normal verification

`VerifySemanticSource` now accepts optional sorted `CapturePreservation`
certificates from the unchanged file/profile/body. The local proof merges
these PCs with instruction order, validates that each names a capture-capable
operation, and removes only the matching barrier. Duplicate, unsorted,
non-creation and unconsumed sites refuse admission. Literal object/array
creation is also a barrier because its buffer may install captured methods.
Neither initialization nor store-domain checks are weakened. The merge adds
O(I + C) quota-charged work and no new storage.

The existing capture-summary API is now a public source-layer header, shared
by production owners and the single-method source CLI. The CLI builds actual
child summaries, charges their live storage/work alongside IR and lookup,
and retains `ClosureWrites::kUnknown`; no blanket premise is introduced.
It also accounts for retained proof, region and named-scope allocations when
admitting subsequent stages.

ABC-only ProgressBar inspection with `--function-source` now passes PC130
and stops at PC200 (`kInternalValue`), the saved result of multiplication at
PC197. This is the next register-value-flow obligation, not a class-source
success. CLI compilation and read-only review completed; tests/RSS remain
paused. Certificate rejection, mixed proven/unproven creators, buffer
barriers and low-budget cases still require regressions when testing resumes.
