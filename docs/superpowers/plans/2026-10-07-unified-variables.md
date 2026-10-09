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

- [x] Add a safe shared-slot example whose undefined initialization is live, and an
      interfering counterpart; source execution/mutations must distinguish both.
- [x] Remove Coalesce::CompletedStore; do not move it under another helper name.
- [x] Preserve live undefined as an explicit initialization at its original point;
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

Rules180 implementation substep: native declaration commits are explicit in
RecoveryFlow; PlanDefinitionElisions consumes shared liveness and inert-write
facts, and its production delta removes only dead private assignment writes.
RHS evaluation stays. Storage analysis rebuilds afterwards. This is partial task3
progress; common initialization, live-undefined preservation during storage
sharing, removal of CompletedStore and convergence scheduling remain open.

Rules181 substep: a shared backward solver now offers distinct all-read liveness
and observable-root demand results. It discovers def-use dependencies on demand,
closes discarded-region/definition demand before one batch commit, and avoids
materializing the full read/definition relation (which exceeded the existing
402-selection budget). This closes the dead-write / inert-RHS interaction only;
shared initialization, live-undefined storage mapping, declaration recovery and
the overall pass coordinator remain incomplete.

Rules182 substep: a separate binding-elision proposal consumes FlowBinding
exposure and the definition-elision delta on the same immutable flow. Only
private undefined declarations without any retained reference/other definition
are omitted. Both proposals commit after analysis; storage then rebuilds its
facts. No lifetime decision is added to Coalesce, and no extra graph construction
is required for this dependent proposal. General initializer/lifetime recovery,
CompletedStore retirement and full pass coordination are still open.

Rules183 prerequisite: the shared flow now projects committed storage identities
for reads and successful definitions, including both update halves. Producer
passes report changes; after storage/expression/loop-declaration changes the
coordinator rebuilds that mapped flow for dependent demand/declaration cleanup.
Final counts/effects follow this commit. Tests include a write whose later use is
visible only through the shared identity, plus the erroneous stale-graph deletion.
This does not complete initialization/lifetime proof or global pass convergence.

Rules184: Coalesce's CompletedStore call is removed. The independent
PlanStorageInitializations consumes shared liveness and mapping proposals, retains
live undefined as a store at its original statement and checks destination
initialization with common completion dominance. Existing source visibility and
activation proofs still apply. The printer consumes an explicit undefined_stores
action; the rebuilt graph distinguishes this write from lexical initialization.
Straight and repeated-loop cases, interfering old values, target TDZ and independent
missing-reset mutants cover the new behavior. Shared general initialization,
copy/constant CompletedStore uses and broader lifetime recovery remain incomplete.

Rules185: RecoveryFlow now emits typed binding references. Coalesce consumes the
current occurrences instead of reconstructing references from the original arena,
and uses one shared FlowInitializations result for reference order and preserved
undefined planning. Its declaration-control/region and native-ready completion
checks are removed. Eliminated versus retained early-write source cases prove the
distinction. Other InitializedRead/effect and lexical activation consumers remain
legacy; the complete task2/task4 initialization/lifetime migration is still open.


Rules186: one const AnalyzeExpressions pass now supplies result-binding,
normal-store and evaluation facts to projection consumers and final publication.
The old two analysis entry points are removed. Inert-statement deletion has a
separate const proposal and batch commit. Analyses remain local and freshly
computed after mutations; projection revision management and global convergence
are not delivered by this change. InitializedRead and activation migration remain
open. Nested redundant writes/read choices exercise final summary consistency and
independent source behavior with original/reversed arenas and geometric sizes.


Rules187: Constants replaces PrivateCopy/CompletedStore with shared all-read
liveness and AnalyzeFlowReadCoverage, without an eager read/definition product.
A separate literal proposal table is solved before committing any plan edits.
An assigning branch with an abrupt alternative now permits constant recovery at
common later reads, while omitted writes/undefined and exception paths remain
protected by the same semantic facts. Dynamic occurrence identity, other legacy
copy/snapshot consumers, InitializedRead and activation migration remain open.


Rules188: Aliases shares AnalyzeBindingReadCoverage with Constants and obtains
fresh facts after literal commit. CompletedStore/PrivateCopy and declaration
initialized_first no longer decide alias availability. Both source and copy
coverage preserve the dynamic producer value; name/activation and escaping state
remain separate obligations. A copy-or-return branch now restores common later
reads to retained source storage. Existing loop/exception/capture and object
identity gates remain. Snapshot motion still owns the last CompletedStore use;
full activation, alias reference-event migration and pass convergence remain open.


Rules189: snapshot availability and single-use counting consume shared coverage
and actual flow references. CompletedStore/PrivateCopy and unused metadata are
removed. Demand cleanup now precedes snapshots; snapshot proposals are batched.
RunPass centrally invalidates graph/liveness after actual changes and shares const
facts through no-op passes. Forwarding preserves other RHS reads, so dependent
demand cleanup repeats only after later storage/declaration changes. A discarded
initial undefined read no longer prevents direct-return recovery; observed initial
values remain. Motion epochs/control regions and lexical environment signatures
still require common effect/activation migration; global convergence is not done.


Rules190: alias reference enumeration joins the common flow. Name/lifetime queries
retain typed binding and occurrence identities from FlowReference; the redundant
SymbolExpr census is gone. Copy propagation, lexical resolution and activation
remain separate obligations. Compound-expression/name-shadow combinations test
this shared path without new production shapes. InitializedRead and canonical
activation, effect-motion migration and global pass convergence remain open.


Rules191 (2026-10-08): completion-graph initialization now precedes expression
effect refinement. InitializedRead and its whole-binding first-read/region
metadata are removed. Typed pass contracts centrally distinguish preserved
positive occurrence proofs from invalidated graph-indexed analyses. One graph
is refined in place; direct CSR reuses the existing LT implementation, and
prepaid graph buffers are rebuilt without retaining semantic contents. New
TDZ-before/dead-read-after declaration cases close the model-to-source path.
Default-parameter programs still retain conservative effects because the flow
models body entry only. Canonical activation/IR, declaration/motion obligations
and global convergence remain unfinished.


### Rules192 continuation: move invalidation before mutation

- Implemented preparation-local projection revisions and analysis/commit phases.
- All eight variable passes enter the common boundary before their first write;
  no-op passes preserve facts and version. Deleted pass-returned changed flags.
- Analysis entry points forbid requests during commit. Scheduler cleanup follows
  actual revision changes, including loop declaration syntax metadata.
- This is integrated lifecycle control, not global convergence or compile-time
  ownership of all legacy plan fields. Remaining motion/activation work stays open.


### Rules193 continuation: shared discarded operand projection

- Moved inert comma-left elimination out of the printer into a separate pass.
- Schedule after stored-result elision and before dependent demand/snapshot
  cleanup; final normalization consumes newly exposed inert operands.
- All consumers follow the shared replacement; printing only transports source
  ranges and the value-result category. No parallel semantic inference remains
  for this optimization in the printer.
- Added a red/green composition case where dropping an inert read enables dead
  write removal while preserving the RHS, plus required coercion counterexamples.


### Rules194 continuation: shared observations for snapshot motion

- RecoveryFlow publishes intrinsic event effects and refines them alongside
  completion facts; semantic reads and definitions retain their own identities.
- Snapshot epochs consume current events instead of a separate syntax census.
- Deleted expression-statement tracking and the associated local predicate.
- Red/green discarded-operand composition and required-effect counterexamples
  validate actual forwarding and execution. Legacy control/activation/lifetime
  motion proofs still need migration; no new local shape admission was added.


### Rules195 continuation: common must-execute motion order

- Added and integrated linear universal next-observation analysis on RecoveryFlow.
- Removed effect epochs, control-prefix comparisons and their legacy metadata.
- Source completion/read identities drive constant-time candidate queries.
- Transparent split/join red/green case, debugger and prior exception/loop
  adversaries, 4,096 small-graph configurations and geometric budgets verify it.
- Handler/activation region identity and lexical lifetime remain explicit pending
  migrations; this step does not claim full expression motion or global convergence.


### Rules196 continuation: execution count and handler identity

- Integrated reverse must-observation history, sharing the forward solver.
- Reference events retain explicit throw/finalizer contexts through refinement.
- Replaced same-region matching with bidirectional write/read pairing and context
  identity. Removed all associated region/guard census fields and traversal state.
- Added once-only/repeated-loop composition and independently demonstrated that
  dropping either new obligation admits a semantic counterexample.
- Lexical lifetime, fuller activation/unwind, original IR and global convergence
  remain pending; this completes the snapshot region restriction replacement.


### Rules197 continuation: complete loop grammar proposals before commit

- Extracted const PlanLoopDeclarations from the mutating loop traversal.
- Typed deltas fully describe suffix starts, demotions and native declarators.
- BeginCommit precedes only delta application; no later decisions read the
  partially changed plan. Final prefix counts remain derived after all passes.
- Multi-loop order invariance and idempotence tests supplement existing source
  execution tests. Whole-pipeline dependency convergence remains pending.


### Rules198 continuation: compound values through copy propagation

- Copy sources now come from shared expression normal-result binding facts.
- Storage removal separately preserves required RHS evaluations and completions.
- Discarded-owner references feed a sparse multi-source retention worklist,
  replacing direct-RHS lookup and the single blocked-source slot.
- Added positive compositions and source mutation/undefined/catch/branch
  counterexamples with independent execution. Whole-pass convergence and fuller
  canonical activation remain open.


### Rules199 continuation: shared expression value use

- FlowValueUse records observed/discarded/unmodeled occurrences in RecoveryFlow.
- The existing inert-operand pass now consumes it to clean unused sequence
  results after copy elimination while preserving required evaluations.
- Replacement edits are fully proposed before commit; final printer remains a
  consumer. Nested composition and counterexamples verify value/effect separation.
- Global pass convergence, activation/unwind and original IR migration stay open.


Task 4 implementation substep (rules230): RecoverySchedule now owns typed
pass/fact dependencies, projection revisions, commit phase, stage order and
ordered demand-cleanup checkpoints. Fact providers remain lazy and check the
active contract. The old constants_changed/value_cleanup_needed/cleanup_needed
scheduling booleans have retired. Cleanup is published only after the independent
DeadDefinitions/InitialValues/Bindings passes complete in order; callers cannot
mark it clean manually. Dirty value projections cannot enter storage or grammar.
Existing initialization transport obligations remain explicit; no new availability
proof is inferred. A component checkpoint is not global pass convergence, so
task 4 and the original milestone remain incomplete. Per-read normal-value
precision remains the next model-to-source extension after this prerequisite.
Selected current verification and fixed-record evidence are in status.md.

Task 2/4 implementation substep (rules231): completed private primitive reads now
consume common incoming liveness/completion facts. Binding invariants freeze
before contextual read/expression/definition refinement on one unchanged
projection. Constants owns all read proposals before commit; version invalidation
and independent fresh cleanup precede aliases/storage. Binding-wide conflation of
different sequential values is no longer the decision limit for those reads.
Unknown, TDZ, source-local/observable cells and updates remain conservative, with
layout permutations, independent finite-set/read/source oracles and adverse value
substitutions. This extends actual generated source, not merely interfaces. Full
original IR/SSA correspondence, activation/capture/finally, nonprimitive read
identity and global pass convergence remain open; task 2/4 and the stage-one Goal
remain active. status.md records the precise selected evidence and paused scope.

Task 2/4 implementation substep (rules232): the sole declaration-placement pass
now follows normal values and fresh independent cleanup, before alias/storage
coverage. The early call is removed; its commit dirties the revision checkpoint.
All-completion dominance and lexical extent remain the proof. Placement still
models initialization completion; a trial after all storage lost coverage and
was retired. Common projected scopes and actual group reference extents fix repeated-loop
representative bias: prefer a covering candidate over a noncovering one; equal
coverage keeps existing ranks. No merging assertion or post-proposal completion/
name/lifetime proof is weakened. Unconditional wide preference regressed record
loop spelling and was retired. Full feasible representative search stays open.
Independent source harness explicit undefined and producer mutants are corrected.
No extra round or source-form admission is introduced. General lifetime/capture/
finally, original IR/SSA and global convergence stay open; status.md records the
actual bounded evidence. Stage-one Goal remains active.

Task 2/4 implementation substep (rules233): interference membership closes before
representative choice. Every original member is ranked against the final shared
lexical and reachable-completion extents; union-time two-winner retention retires.
Source availability reuses the existing current initialization dominator facts,
with distinct lexical/execution coordinate types and one shared reference scan.
The storage proposal remains immutable until complete; schedule revision and
commit invalidation rules are unchanged. Independent completion/name/lifetime/
cell/initial-value checks remain, with original RHS effects at their occurrence.
An independent 45,056-case all-candidate oracle and native initializer/producer
source compositions verify the change with layout/budget/adverse cases. Full
feasible name/cell/value choice, original IR/SSA, activation/capture/finally and
global pass convergence stay open. status.md records current selected evidence;
the unified-variable milestone and stage-one Goal remain active.

Task 2/4 implementation substep (rules234): typed expression-occurrence transport
replaces the whole-expression-statement snapshot limitation. Shared discarded
value-use, successful write/read pairing, motion/exception and scope/lifetime facts
prove removal of the source evaluation while preserving sibling effects. One
common trial adapter provides actual owners and nested transport-dependency roots;
independent moves sharing a statement do not share rejection. Proposal validation
is read-only; commit uses owned IDs, invalidates the revision and removes only
binding-owned declarations/completions. Storage receives fresh facts and later
normalization/cleanup retain their separate order and contracts. Printer ownership
and moved RHS spans consume the same projection. Independent source cases and
mutants cover observed results, prefixes, nested native initialization and dependent
versus unrelated scope failures, with reversed arenas and geometric sizes. No
extra round or source-shape admission is added. Original IR/SSA, full activation/
capture/finally, feasible storage and global convergence remain open. status.md
records selected verification; unified-variable milestone and stage-one Goal active.

Task 2/4 implementation substep (rules235): publish canonical binding regions
from the existing once-owned lexical traversal. Common BindingRegion/BindingInScope
serve both original and projected declarations; pure storage candidate ranking
consumes those facts before independent name/completion/lifetime/value proofs.
Original scope containment is no longer deferred entirely until after representative
selection. Immutable canonical syntax facts and revision-owned projected selection
remain distinct; each commit invalidates common facts and all proposal validation
precedes mutation. Existing pass order and stage/cleanup contracts remain unchanged.
Source nested-scope/sibling-shadow/outside-return cases, independent ancestry and
semantic mutants cover the precision correction with layout/scaling/budget checks.
No new production shape admission. Status.md records actual selected evidence;
originalIR/SSA/full lifetime/capture/finally/complete feasibility/global convergence
remain open. Unified-variable milestone and stage-one Goal remain active.

Task 2/4 implementation substep (rules236): shared canonical spelling facts
publish nearest shadows and uniqueness through RecoveryFlow. Coalesce's independent
uniqueness census retires. Final interference groups index actual reference positions;
candidate shadow regions use binary-search interval queries rather than per-variable
walks or bounding-box rejection. Group interference construction and independent
final name/init/lifetime/cell/value proofs remain. Fresh projection/atomic proposal-
commit contracts and existing pass order remain; no extra round/retry is introduced.
Independent source active/harmless shadows, real wrong-name/update mutations, tiny
name/candidate oracles and hole/reordered/scaling/budget cases cover the implementation.
General pass convergence needs dependency/incremental cost ownership and remains
open along with originalIR/SSA/full lifetime/capture/finally/rewritten visibility.
Status.md records selected acceptance; unified-variable milestone/stage-one Goal active.

Task 2/4 implementation substep (rules237): pure current graph binding-name retention
and sparse canonical-shadow projection replace all-original-symbol activation in
current alias/storage/snapshot name decisions. Every retained definition/reference
and observable binding preserves its name; undefined initialization is a definition.
Retired private temporaries exit the name model only after common earlier passes
remove syntax. Value/lifetime/effect/delete obligations remain separate. Existing
revision invalidation, stage order and demand-cleanup contracts remain; no extra
pass round. Independent retained masks/ancestor oracles, source execution/mutations,
layout/200-201/geometric/budget/reuse negatives verify the substep. Complete rewritten
native-for namespace/prospective storage retirement, originalIR/SSA/full activation/
capture/finally/global convergence remain open. Actual evidence is in status.md;
unified-variable milestone and stage-one Goal remain active.

Task 2/4 implementation substep (rules238): canonical typed spelling identities
feed one current retained lexical-name range index. Current regions, not original
statement scopes, own symbol/free-name lookup and current shadow publication.
Explicit enclosing environments distinguish actual external activation from missing
local regions; unknown/conflicting facts retain typed gaps. Original name-query
paths exit aliases/storage/snapshots. Independent value/cell/initialization/lifetime/
effect proofs and revision/commit/stage/cleanup contracts remain; no new pass round.
Native declaration and native-for outside-read positives/inside-read negatives,
independent ancestry/projection/mask oracles and syntax/execution/mutation/layout/
scaling/budget/reuse checks verify the substep. Actual evidence belongs in status.md;
prospective simultaneous retirement/renaming, originalIR/SSA/full activation/capture/
finally/global convergence and the overall milestone/stage-one remain open.


Task 2/4 implementation substep (rules239): pure common name-retirement analysis
consumes the complete value/effect-safe copy proposal with fixed retained roots.
A prospective lexical baseline, root/position query partitions, successor deletion
and a monotone activation/conditional-demand queue replace Aliases' old-namespace
rejection and optimizer-local blocked-copy graph. Independent value/cell/lifetime/
TDZ/effect/deletion proofs remain; first-commit invalidation, owned deltas and typed
stage/cleanup contracts remain. Source tests allow legitimate preceding storage
recovery; independent tiny ancestry/projection/copy-demand oracles test the closure
rather than require a particular intermediate plan. Actual acceptance belongs in
status.md. Prospective storage-group renaming/retirement, original IR/SSA/full
activation/capture/finally/global convergence and the overall milestone remain open.

Task 3/4 implementation substep (rules240): complete fixed-root storage proposals
consume the same common namespace-retirement closure as copies. All mapped reads,
writes and live-undefined writes are unconditional queries; completion/lifetime and
initialization failures are separate retention demands. Member-local initialization
actions remain valid under subset rejection and are computed once on unchanged
facts. Semantic representative feasibility precedes the potentially obsolete current
shadow preference. Old local namespace/blocked-mask/preserved-write filters exit.
Source two-group mutable positives/local/interference negatives and tiny prerequisite/
subset/throw/resume oracles verify the boundary. Actual acceptance is in status.md;
arbitrary rename/relocation/new-root selection after rejection, original IR/SSA/full
activation/capture/finally/global convergence and the overall milestone remain open.


Task 2/4 implementation substep (rules241): a version-owned completed snapshot
attempt is refreshed once after storage/alias normalization and complete ordered
demand cleanup if the projection changed. Initial snapshot normalization remains
before interference contraction. The checkpoint is consumer scheduling state, not
analysis validity; first-commit invalidation and owned proposal deltas still apply.
Wrong-stage/active/dirty and clean-partial cleanup queries are rejected. Motion and
lexical environment indexes are lazy without new admission. Storage-shadow retirement
positives and local/interference TDZ negatives are independently executed, mutated
and tested under reversed arenas/200-201/geometric composition. Actual acceptance is
in status.md; this bounded stage dependency repair does not complete global pass
convergence, original IR/SSA/full activation/capture/finally or the overall milestone.


Task 2/4 implementation substep (rules242): extend the shared flat normal-value
solver with proved opaque entry identities and all-definition joins. An independent
BindingValues read-substitution pass runs in explicit ReadValues after storage and
snapshot destinations, before Grammar; backward alias/storage/motion scheduling is
forbidden. This order preserves the existing shadowed snapshot representation.
Mapped-argument metadata is adapted once; effects/completion/storage/deletion stay
independent. Current flow, expression/name analysis and printing consume the same
read projection. Repeated branch/loop copies, initial undefined and effectful RHSs,
root/local/shadow/mapped negatives, independent origin/expression oracles and source
execution/mutants/layout/scaling/budgets verify the substep. Actual acceptance is
recorded in status.md. General SSA/catch/capture grounding, per-read symbolic values,
final namespace allocation/full activation/finally/global convergence remain open.


rules243 implemented substep: BindingProjection centralizes committed storage and
read interpretation. ReadValues is indexed by the canonical slot published by the
preceding alias/storage owners; common flow and source consumers compose it in that
order. Writes/update halves keep Storage. Shared repeated branches and effectful
copy output migrate through independent demand cleanup; current name/scope facts,
revision invalidation and owned proposals remain prerequisites. New layout variants
permit a different legal representative spelling while preserving required variable
count and independent behavior. No new sample/shape condition or pass retry. Final
selected verification is recorded in status.md; the overall milestone stays open.


rules244 implemented substep: CurrentFlow consumes shared normal-completion binding
results for successful assignment/initializer copy facts. Direct-read indexing exits
the ordinary production path; explicit unknown results do not trigger a fallback
shape inference. Interference/coalescing and independent effect/definition cleanup
remain separate. CurrentFacts owns exact revisions; trial projections omit current
normal-value precision. Source sequence/choice/logical/prefix/assignment/native-init
composition and live/postfix/exception counterexamples verify the migration. Final
selected evidence is recorded in status.md. Trial value precision, originalIR/SSA,
full activation/capture/finally and global convergence remain open.


rules245 implemented substep: shared normal values publish symbolic results at actual
completed reads, independently of all-definition invariants. Late BindingValues owns
both proposals; each checks current scope/names before first-commit invalidation.
One BindingProjection supplies occurrence-before-slot read interpretation to flow,
expressions, class names and printing, preserving write identity. Shared liveness
publishes invocation reachability once, consumed by contextual joins and current
snapshot motion; conservative live/global/name obligations stay independent. Mixed
prior/liveundefined/effectful-catch and semantic negatives plus independent graph
oracles, actual source execution/mutants/layout/scaling verify the substep. Final
selected evidence is recorded in status.md. Overall milestone remains incomplete.

rules246 implemented substep: initialized-read occurrence evidence owns its source
projection revision; all production expression/value consumers require the current
analysis revision. First commit explicitly transports or revokes that evidence
under the existing typed pass contract. The unversioned cache/preservation boolean
decision path exits; other facts still invalidate atomically. No-op and batch
semantics, all pass categories and stale/phase/geometric counterexamples plus
existing source execution verify ownership. No new recovery admission or graph
analysis. Final selected evidence is recorded in status.md; overall milestone and
original correspondence/full lifetime/global convergence remain incomplete.


rules247 implemented substep: shared sparse FlowCopies proves current-entry equality
at completed private read occurrences through normal-result gen/kill and all
reachable completion paths. Successful source/unknown destination writes revoke
relations; failed RHS paths retain them. Global immutable roots remain separate.
Late BindingValues declares Copies/Liveness/Flow/Expression dependencies, owns
merged proposals and separately proves scope/names before aggregate invalidation;
subsequent cleanup consumes fresh facts. No backward pass, extra round or printer
analysis. Mutable entry reads, loops, refresh/exception/shadow/mapped/prefix/postfix
source compositions and independent concrete small graphs verify the substep.
Actual selected evidence is recorded in status.md. Local/catch/capture activation,
original correspondence/full lifetime/global convergence and milestone remain open.


rules248 implemented substep: common all-read liveness includes proof-only current
source-value queries at successful normal-copy definitions. Observable demand
ignores those queries. FlowCopies inherits entry-root equality through private
intermediates using the same sparse dependency solver, with root kills, independent
intermediate writes and exception bypass. Direct-entry-only generation leaves the
private chain path; no extra pass round or layout/copy-depth match. Independent
two-cell graphs, split/reversed/geometric chains and actual source/mutants verify
composition. Actual selected evidence is recorded in status.md. Local/catch/capture
activation, original correspondence, trial precision/fullfinally/global convergence
and overall milestone remain open.


rules249 implemented substep: builder-owned typed statement completion/progress and
current event ownership feed one shared observation fold. InertStatements consumes
kStatements at exact revision; old expression/declaration syntax decisions retire.
Final read cleanup exposes ordinary pure controls before grammar without another
round. Retained definitions, abrupt completion, suspension, possible repetition and
conservative cleanup remain independent obligations. Printer only marks all omitted
origins. Independent forest reference/source/layout/budget evidence is in status.md;
full termination/finally/activation/originalIR/global convergence remain open.


rules250 implemented substep: graph SCC facts discharge modeled noncyclic repetition;
indexed actual normal-exit matching and difference propagation discharge local jumps.
All return/throw/resume/cleanup edges are independent obligations, including graph-only
markers. kCycles shares exact revision and lazy ownership with kStatements, no pass
round or backward stage. Old always-repeat/always-abrupt jump behavior retires where
common proof exists; legacy missing facts/fullfinally remain conservative. Dense graph
and ancestor-chain references/source variants are recorded in status.md. Initialization,
activation, original correspondence and whole milestone remain open.


rules251 implemented substep: adapter-owned mandatory entry expression/events feed
complete and residual statement observations. Residuals exclude only own entry
completion, never full child effects/definitions/lifetimes/return/jump/cleanup/cycles.
Independent late StatementValues waits for final ReadValues/demand, owns proposals
before first commit, preserves evaluation at its occurrence and invalidates facts;
independent cleanup then grammar consume the same projection. No backward stage,
whole-function iteration, shape matching or printer analysis. Actual receiver,
conversion, assignment, exception, generator and async source comparisons plus
independent forests/layout/scaling/protocol tests are recorded in status.md.
Multi-branch program synthesis/fullfinally/activation/original correspondence/global
convergence and overall milestone remain open.


2026-10-09 user priority after rules251: finish this pass, then resume the five
backbone delivery loops before any further appearance optimization: original IR/SSA
correspondence, ordinary local/catch activation identity, shared trial precision,
dependency-driven bounded pass convergence, and complete unified-variable acceptance.
All five remain open. Start with original IR/SSA integration and carry the proven
current source projection facts forward; do not replace these loops with local fixes.
Existing paused verification/corpus/resource/agent scopes stay paused.


2026-10-09 rules252 Task1 substep: final TreeSemantics retains compact original
def/use/CFG/phase/effect/origin facts and actual operand/result correspondence.
Ordinary/family/record/static-field production passes this same immutable input
to source recovery; source normal domains now reach common effect analysis and
real emitted-source elimination, while child GetValue/conversion remains. The
original-domain consumer removal mutant fails; independent original-input source
fixtures cover copy/register/block/arena changes and boundary/geometric/exact-short
cost evidence. One language effects table serves all clients; no second ISA table,
new layout admission, storage root shortcut or additional pass round. Full original
operation/reference/phase mapping, ordinary binding activation, trial precision,
full dependency convergence and milestone acceptance remain OPEN. Other appearance
optimizations stay paused until these five delivery loops close.

Task1 rules253 substep: public property references now publish acquisition,
receiver, key conversion, Get/Set and successful continuation in the common
source graph. Original base/key UseId ranges attach through the existing index.
Motion/demand/refinement consume these phases; generic child/single-effect
member handling exits the decision path. Versioned source semantics use2025
deferred keys, documented against primary specification and independent Node
execution; the initial2024 assumption was withdrawn. Complete original
operation/region/protocol phase projection remains OPEN, as do the other four
backbone delivery loops. No appearance optimization or paused work resumed.


Rules255 backbone substep: normal domains now consume the exact common source
success definitions and sparse all-read liveness, with original SSA parameter-entry
facts connected through the checked mapping. Direct old raw occurrence-domain
consumption leaves recovery decisions. Current/trial bootstrap owns its own type
product and rejects external cached spans; no new pass round/order is introduced.
Independent state-oracle, success/throw/loop source execution and stale-domain/
early-write/dead-init mutants verify the component. Tasks1/2/4/5 and all five human
backbone loops remain open; see status.md for final bounded verification evidence.


Rules256 Task1/3 backbone substep: actual successful source assignments retain
original result/phi/saved/catch correspondence from the existing proof owners.
Current and trial normal analysis consumes its stricter domains only after shared
operand/completion change propagation. Original ExprId hints stay provenance;
changed producer/path/statement deltas cannot reuse the old proof. The common
completion context is sparse and built once, with finite-lattice closure and
accounted memory/work. No independent optimizer inference, new admission pattern,
pass round, budget increase or paused scope resumes. Existing old-value numeric
update lowering remains unmapped rather than confusing its result with the stored
increment. Final bounded evidence belongs to status.md. Complete raw protocols/
effects, ordinary activation, lifetime trial deltas, dependency convergence and
unified-variable final acceptance still require their delivery loops to close.


Rules257 Task2 backbone substep: publish canonical ordinary local/catch allocation
sites once, initialize catch after scope creation, and share activation gen/kill
across demand, liveness, reaching definitions, coverage, constants, copies and normal
domains. Common completion analysis requires allocation before initialization;
completion-only synthetic facts never gain cell identity. Old catch ordering and
cross-activation value propagation leave their decision paths. No pass round/order
or independent local admission expansion. Current/trial flow uses the same adapter,
but complete projected declaration/lifetime deltas and abrupt unwind remain open.
New independent graph/state, reversed event, exact/short budget, 200/201/geometric
and repeated catch/local emitted-source cases remain bounded by existing selected
targets. Final evidence belongs to status.md. All five backbone deliveries remain
open, with no appearance optimization or paused-scope resumption.


Rules258 Task2/3 backbone substep: the shared current/trial adapter publishes typed
before/after canonical lexical contexts, indexed once in immutable FlowScopes.
Every value client uses TransferBinding on completion edges; definite abrupt
break/continue/return/throw departure ends the unit, while unknown cleanup keeps
may-demand/reaching definitions and invalidates substitution. Initializations also
checks active-unit presence and owns a shared index. Point-only transfer and departed
unit dominance paths retire without local shape admission or pass rounds/order/
budget changes. Legacy value/completion-only inputs retain demand/order, never cell
identity. Independent ancestry/oracle, reversed scope IDs, lifetime ownership,
unknown-demand and adverse trial tests plus emitted-source copy/block variants and
production mutants verify the component within existing selected gates. Status.md
owns final evidence. Complete projected/native-for/capture/finally lifetime deltas,
full original correspondence, dependency convergence and final acceptance remain
open; all five backbone delivery loops are OPEN and other optimizations stay paused.


Rules259 Task2/3 current-lifetime substep: one-time lexical census publishes explicit
current region allocation correspondence. Projected declarations use their current
owned block unit, retiring original Symbol.scope fallback; missing/omitted block
allocation correspondence is unmodeled. Native-loop regions explicitly retain
preexisting completion/value capability with no unit/cell identity certificate;
existing placement/confinement/capture prerequisites are not expanded. Shared
current/trial consumers and revision invalidation remain unchanged, with no new pass
round/order/budget. Independent positive/adverse projection, reversed scope/region,
exact-short budgets and emitted narrowed-source200/201/400/800 variants plus production
mutants verify the component in existing targets; status.md owns final evidence.
Complete native-for allocated/copied per-iteration environments, capture/finally deltas,
full original correspondence, dependency convergence and unified-variable final
acceptance still require delivery closure. All five backbone loops remain OPEN;
other appearance optimizations and paused scopes remain paused.


Rules260 Task2/3 entry ownership substep: current/trial preparation and the final
grammar pass share one complete LoopDeclarations entry product. The source adapter
restores outlined prefix assignment/completion and actual enclosing occurrence/
binding ownership, while native declarators retain the explicit legacy value-only
contract. The common flow/name/initialization clients consume that projection;
prospective-loop ownership no longer decides prefix semantics there. kLoopEntries
is a declared provider dependency, with no new pass round or stage. The existing
lexical census supplies descendant/root identity once; trial omissions invalidate
the suffix independently. Component negative/model/scaling/mutation tests remain
in existing targets; status.md records actual final 31/63/fixed-record evidence.
Actual loop allocation and copied iteration units, captured/finally phases, full
original correspondence, dependency convergence and milestone acceptance remain
required. All five backbone delivery loops stay OPEN, other optimizations paused.

Rules260 continuation within the same substep: independent source inspection
rejected adapter-only publication, because later alias cleanup expanded the native
suffix around a formerly outlined binding used after the loop. Placement now
stages its entire initializer proposal through the common grammar provider before
commit and leaves prefix candidates in their original binding. No later pass can
resurrect their native annotation. This closes that concrete cross-pass ownership
error; full allocated/copied environments and all five deliveries remain open.


Rules261 Task2/3 actual native entry substep: complete grammar groups and actual
retained BoundNames now publish binding-indexed initial allocation provenance.
Current/trial FlowBuilder emits one initial environment event before every RHS,
shares allocation gen/kill with value clients and allocation-before-completion
with initialization. Lexical region membership alone never grants this proof;
a new peer-binding RED rejected the preliminary region-wide implementation.
The old absence of actual header allocation leaves the modeled member path.
No extra round/stage/order/budget or paused scope resumes. Copied iteration
values/cells, actual header/body phase relocation, captures/finally, full original
correspondence, dependency convergence and milestone end-to-end closure remain.
All five human backbone deliveries stay OPEN; status.md records final bounded gates.


Rules262 Task2/3 phase-program substep: production current/trial preparation now
consumes the same immutable LoopSyntax used by printing. The common flow separates
condition/body/increment/exit, places expression-clause operands outside the body
activation, executes retained continuing arms once inside it, and routes ordinary
continue/normal completion through increment. Scope/effect/exception and ownership
facts share these positions; the modeled path no longer wraps all phases in the
raw body scope. Legacy synthetic flow explicitly lacks source phase capability.
No syntax admission, round/order/budget change or paused scope resumes. Copied
old-value reads/new definitions/new units and capture/finally transport are still
required. Complete original correspondence, trial lifetime deltas, dependency
convergence and final acceptance remain open; all five delivery loops stay OPEN.

Rules262 final gate exposed required grammar retention, not a sample admission:
the empty native body became inert/omitted while its moved predicate retained
source ranges. Existing declaration-value range/source tests rejected the initial
plan. FlowStatementRetention now represents mandatory grammar slots separately
from effects; inert elision consumes it and removes its old tree.body exception.
Original range assertions remain unchanged. The first final runner was stopped
on this real regression; final evidence must come from the corrected frozen build.

Rules263 Task2/3 value-plane substep: actual source native first/repeat scalar
transport now belongs to the common current/trial flow provider. Explicit
captured inputs feed shared liveness/demand/reaching definitions/constants/types;
available-current-root substitution stays conservative. Native member indexing,
phase ownership, cross-reset/dead-input/mutable-root negatives, layout/scaling and
seven production mutations supplement existing independent source checks. First
mutation sweep exposed a disconnected-entry coverage gap; invocation dominance
now checks execution, not merely node presence. status.md records final evidence.
No pass round/order or hand-maintained analysis refresh is introduced. Per-iteration
allocation, reset-aware initialization, cell/capture identity, original IR/SSA,
full trial lifetime deltas, convergence and milestone final acceptance remain
required; all five backbone delivery loops OPEN and other optimizations paused.


Rules264 Task2/3 backbone substep: native first/repeat scalar programs now own
CURRENT-unit replacement and normal/owned-break exit events, using a shared
native forest. Missing native context is unknown, preserving old-value may demand;
conservative cleanup guarantees body ancestry only. This does not certify physical
heap allocation order, captured cells or every source-entry corridor.

Shared reset-aware must-initialize replaces Completed's old declaration-dominance
and private scope decision. Declarator anchors remain placement metadata. Storage
proposals request representative states explicitly from the same immutable flow;
analysis queries do not update plans. Basic-block local actions, reachable-gen
bottom indexing and batched heavy-path/Fenwick kill certificates avoid per-binding
whole-flow walks under unchanged budgets. No new pass/round/order or local shape
admission is added. Independent graph/scope oracles and eight production mutants
plus the original frozen 31-target/63-test/fixed-record gate verify this component;
status.md carries final actual evidence. Nested native model input supplies its
outer phase program explicitly, not new syntax discovery.

All five backbone delivery loops remain OPEN. Next mainline precision must handle
ordinary/source-created entry and movement unit facts, then complete raw IR/SSA
correspondence and trial lifetime deltas, dependency reruns/convergence and the
milestone end-to-end acceptance. Other appearance optimizations, agents,
fullsuite/expanded corpus/RSS/benchmark/DFX remain paused.

Rules265 Task3 backbone substep: BindingPlanView supplies all candidate semantic
facets to expression/type/grammar/flow bootstrap. Declaration sites are explicit;
Declarations stages initialization, lifetime placement and old-declaration
retirement together, validates shared current-unit completion and names, then
commits atomically. Native old declarations retire in the same transaction.
RetireDeclaration is shared by trial/commit; required outlined evaluation remains
at its original occurrence. No extra pass round/order or layout admission.

Mixed old/candidate facet authority exits the migrated producer; snapshot final
retirement/subset trials, remaining movement/lifetime inputs, ordinary/catch/
reentry cell precision, raw IR/SSA correspondence, dependency-driven reruns and
final milestone acceptance remain required. All five backbone deliveries OPEN.
Other optimizations and agents/fullsuite/expanded corpus/RSS/benchmark/DFX paused.
Final frozen verification and actual evidence are recorded in status.md.

Rules265 Task4 cost substep: frozen 800-pair source admission revealed unconsumed
derived cache ownership during full trials. RetainFacts at pass boundaries uses
closed RequiredFacts, keeps flow/expression, destroys unused derived products
before credit, and permits lazy same-revision recomputation. No projection edit,
shape threshold, extra round or budget increase. Cache ownership cost is covered
by the existing scaling source gate; dependency rerun/minimal rebuild/convergence
proof remains OPEN. First failed gate and repair evidence remain in status.md.


2026-10-10 rules266 Task3 mainline continuation: full snapshot projection owns
all changed declaration/completion/value facets; shared RetireDeclaration and
Publish remove the old trial/commit semantic split. Common PlanSnapshotRetention
owns actual moved reference/name queries and nested components, delegating shadow
restoration to indexed closure. The exact accepted transaction is analyzed again
before publishing the same arrays. Scratch ownership is destroyed before budget
credit, without raising budgets or introducing per-binding graph retries.

The end-to-end restored-shadow combination alone does not exercise closure:
observation pairing already prevents its adverse move. Independent small common
namespace graphs therefore assert hand-derived restoration, unaffected and nested
selection outcomes; production mutants must demonstrate detection. Follow the
frozen31/63/fixedABC gate and original paused scope. All five backbone deliveries
remain OPEN; continue complete raw IR/value/effect correspondence, ordinary units,
remaining trial consumers, dependency convergence and final milestone acceptance.


Rules267 advances Task1's actual raw SSA consumer: original inline results now
map to current evaluation entry/normal continuation and feed common normal-value
analysis under the same operand/path validity as stored definitions. A missing
store correspondence cannot arbitrarily erase a surviving inline proof. Changed
projections seed actual boundaries; raw operand-hint joins are not certificates.
The existing preparation order remains graph, initialization/liveness, domains,
shared effects. No separate optimizer mutates/reuses these facts.

This is a bounded correspondence substep, not Task1 completion: raw before/commit
phase and exception exit-to-source action relationships still need integration.
Ordinary local/catch/reentry identity, remaining trial clients, dependency-driven
convergence and full milestone acceptance follow that work. All five OPEN.
Frozen 31 targets/63 names/fixed ABC and all paused scopes remain unchanged.


Rules268 substep: retained verified raw catch ownership is consumed by current
activation definitions. Explicit pending payload joins, conservative implicit
failure values, scoped exposed value tracking and success-only stores share the
normal-value solver. Initialization/effect refinement now precedes liveness and
domain consumption; it remains pure and exact-projection local. Decoded and
source-adapter counterexamples exercise payload changes, copies/layout variation,
external invalidation and failed stores. Complete raw phase/action correspondence,
ordinary unit identity, all trial consumers, dependency convergence and milestone
acceptance remain OPEN. Do not proceed to other optimizations before those five
backbone deliveries close. Existing fixed verification/paused work scope remains.


Rules269 substep: the common source semantic table produces normal/before-throw
effects, current source phases publish them, and the shared value solver consumes
UserCode on the actual completion. Required is retained for write/evaluation
observability, not reused as mutation evidence. This retires that coarse current
production decision; unknown graph clients/cleanup remain conservative. Tests
include emitted ordinary local source, a two-completion counterexample, property
phase semantics, reverse IDs, exact/short budgets, 200/201/400/800 and production
mutants. Full raw phase/action correspondence and all five backbone deliveries
remain OPEN; do not resume unrelated optimizations or paused work.

Rules269 final evidence: first frozen63 run retained62/63 with800-binding
printer resource failure; compact field layout preserves complete phase facts
and restores128-byte event without increasing allowances. Repaired targeted2/2
and second frozen31/63/fixedABC all pass; source/input/selection hashes unchanged.
No backbone delivery is closed by this component; next implementation remains
complete raw phase/action/exception correspondence, ordinary units, remaining
trials, dependency convergence and final milestone acceptance.
