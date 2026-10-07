# Unified Variable Recovery Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans natively in the current worktree. Agents remain paused. User has supplied the architecture and authorized implementation; do not request approval again.

**Goal:** Recover ordinary-function temporary and loop-carried variables through one semantic flow, retiring overlapping local decisions.

**Architecture:** Retain verified CFG/SSA/effect/exception-to-source correspondence, add explicit lexical events, and share an immutable RecoveryFlow. Def-use, interference, definition elision and source-binding plans are separate consumers; printing consumes their result.

**Tech Stack:** C++20, existing ValueProgram/TreeFlow/EvaluationPlan, CMake/CTest, independent Node/Acorn tests; no new dependency.

**Spec:** docs/design/source-recovery/unified-variables.md

## Global Constraints

- Current worktree only; preserve f656e763 transition baseline.
- No sample/opcode-sequence/same-block/adjacency/fixed-shape admission.
- Source sidecars stay outside engine inputs; explicit language contracts stay unchanged.
- Full suite, expanded corpus, RSS/benchmarks, DFX, agents remain paused.
- Existing byte/work budget covers retained graph, sparse facts and temporary growth.
- Every verified coherent commit refreshes status.md; whole milestone is not complete until old decisions exit.

## Review Focus

- Live initial undefined across repeated scope activation must survive storage sharing (tasks 1–3).
- Failed conversion/store and catch reads must see the right definition (tasks 1–3).
- Shadow names, source locals and capture cell identity must not become scalar aliases (tasks 1,4).
- Finally/await/yield conservatism must preserve existing output, not invent precision (tasks 1,3,5).
- Forwarded-copy chains and reordered arenas must retain origins with bounded work (tasks 2,4,5).

### Task 1: Shared semantic flow and correspondence ownership

Files: create src/source/recovery_flow.h/.cc and tests/unit/source/recovery_flow_test.cc;
modify tree_flow/tree_semantics preparation and binding_interference.cc, CMakeLists.txt.

Interfaces: `BuildRecoveryFlow` consumes checked FunctionTree plus verified original
correspondence where available; produces immutable typed RecoveryFlow with definitions,
references, completion edges, scope/capture events and source/IR identity maps.
`FindBindingInterference` consumes graph facts, not a FunctionTree syntax walk.

- [ ] Test parameter/undefined/assignment/update definitions, success-only commit,
      branch/loop edges, nested scope/capture, origin identity; reject mismatched revisions.
- [ ] Retain the existing verified original correspondence at preparation boundary;
      source event adapter adds only source-specific semantics, not duplicate opcode rules.
- [x] Extract rules172 private graph into the shared model and remove its Build overloads.
      Transitional consumers explicitly record legacy supplied facts until task2 migration.
- [ ] Run recovery_flow_test, binding_interference_test/source and existing tree-flow tests.
- [ ] Commit with actual implementation/remaining-gap evidence.

Retained-correspondence substep (rules176): TreeSemantics transfers its checked
TreeFlow through ConstructedFunction/ExceptionFunctionDraft. VerifiedFunction owns
that final flow; family and capture verification reuse it. Default-signature tree
mutation replaces the correspondence after revalidation; partial trees have none.
Original SSA/effect facts are not yet connected to canonical RecoveryFlow, so task
1 remains incomplete. No new copy of family correspondence is introduced.

### Task 2: Shared definitions, initialization and liveness

Files: create src/source/flow_definitions.h/.cc, flow_liveness.h/.cc,
 tests/unit/source/flow_definitions_test.cc and flow_liveness_test.cc;
modify binding_interference.cc and binding_initializer.cc.

Interfaces: `AnalyzeFlowLiveness(const RecoveryFlow&, SemanticBudget)` returns
sparse incoming live facts and live definitions. `AnalyzeFlowDefinitions` consumes
that same immutable graph and its liveness to prune forward reaching-definition
propagation. Initialization proof remains a separate obligation; empty def-use is
not evidence of initialization. Interference consumes shared live facts.

- [ ] Add independent small-graph fixed-point oracle for reaching definitions/liveness;
      cover early reads, success/failure writes, diamond/loop and repeated scope entry.
- [ ] Implement shared worklists and indices, preserving original SSA/phi correspondence.
- [ ] Migrate normal_stores/InitializedRead and immutable-copy proof to shared facts;
      remove their local semantic decision branches from the migrated path.
- [ ] Verify oracle equality, 200/201/geometric cases and exact/short budgets.
- [ ] Commit and enumerate retired branches.

Implemented substep: shared backward liveness now drives production interference;
forward def-use is tested against an independent oracle but has no transformation
consumer yet. Tests live in recovery_flow_test.cc to share only graph fixtures,
not production solver logic. Original SSA/phi retention and initialization migration
remain open; task 2 is not complete.

### Task 3: Separate storage sharing from definition elision

Files: create src/source/storage_plan.h/.cc, definition_elision.h/.cc;
modify binding_initializer.cc, binding_interference_test.cc and binding_interference.mjs.

Interfaces: `PlanStorage` consumes flow/definitions/liveness and returns only storage
mapping. `PlanDefinitionElision` consumes that map and the same facts; returns typed
keep/omit/preserve-RHS actions, including live declaration initialization.

- [ ] Add a safe shared-slot example whose undefined initialization is live, and an
      interfering counterpart; source execution/mutations must distinguish both.
- [ ] Remove Coalesce::CompletedStore; do not move it under another helper name.
- [ ] Preserve live undefined as an explicit initialization at its original point;
      eliminate dead definitions independently and retain RHS effects/throw behavior.
- [ ] Run initializer/interference/stored-result tests plus independent JS behavior.
- [ ] Commit with old-condition removal evidence.

### Task 4: Source variable plan and single consumer preparation

Files: create src/source/source_variables.h/.cc; modify tree_printer.cc,
class_names.cc, binding_initializer.cc, loop_initializer/transition consumers.

Interfaces: `PrepareSourceVariables` is the sole coordinator and returns immutable
flow facts plus SourceVariables declarations/storage/elision/expression actions.
Printer and class-name analysis receive the same prepared plan per function owner.

- [ ] Test renamed storage across nested shadows, original locals, native for-let,
      reassignment/const and omitted forwarded origins.
- [ ] Replace duplicated scope/initialization census with common lifetime/name queries.
- [ ] Declare pass fact dependencies and projection revisions; apply deltas atomically,
      invalidate/recompute before consumers, and separate Evaluations analysis from
      replacement mutation. Iterate only simplifying groups with a convergence measure.
- [ ] Move migrated inert/definition decisions out of printer; retain syntax/precedence.
- [ ] Keep legacy adapter only for explicitly unconverted domains; list its obligations.
- [ ] Run loop/local/binding/snapshot/class-name/record-contract tests and commit.

Task 4 implemented substep (rules178): SourceVariables owns nullish/loop/binding
preparation in dependency order. TreePrinter prepares once for its current owner
set and passes const plans to printed class-name analysis. Supplied owners no
longer repeat these passes. External nested class bodies use the same coordinator
but are not yet shared across printer instances. Legacy initialization, version
invalidation and canonical flow integration remain open; task 4 is not complete.

### Task 5: End-to-end milestone acceptance and old-rule retirement

Files: tests/unit/source existing decoded fixtures; tests/integration existing
record tests; docs/design/source-recovery/status.md, acceptance.md,
shape-restrictions-audit.md, unified-variables.md; docs/design/algorithms.md.

- [ ] Exercise decoded register/COPY/block variants and source-arena permutations.
- [ ] Compose loop/branch/exception and conservative capture/suspension boundaries;
      validate legitimate wrong-merge/early-write/dead-init mutations.
- [ ] Run independent generated-JS behaviors and existing PasteBoard/ProgressBar record tests.
- [ ] One fresh paired ABC export, syntax/manifest/source difference evidence; no expanded corpus.
- [ ] Inspect actual call paths: five responsibilities share facts, named legacy branches
      no longer decide migrated cases. Document complexity and remaining precision gaps.
- [ ] Commit. Mark this milestone complete only when all tasks and retirement pass;
      do not mark the overall phase-one Goal complete on this component evidence.
