Choice: design G1's class-self initialization scope; do not implement an admission this round.
Why: two eligible records share this missing proof; no small, fully justified fix emerged from the remaining probes.
Targets: o0/4660 and o20/8166, upper bound two; other static-initialization cases are not counted.
Risk: L / cross-layer; the missing fact joins storage, native class scope and completion, not just a reader option.
Boundary: preserve guards, initialization/observation order, real captures, typed refusals and every parked track.

# Selection report at 1d8326e8

The source cohort now has **584/620 complete, 36 incomplete** (35 refusals and
one partial class), not 37. The old triage table predates the lexical-store,
guarded-arm and decision-literal completions. Fresh exports of both flag sets
and comparison with rebuilt wtbase are in `build/review/next-choice/`.

No production code or tests are changed. The previously ruled-out host argument
snapshot capability is skipped. The class-self evidence below reuses Round 76;
this is a design of its missing joint proof, not another attempt to open the
Closed reader or relax ClassSelfOrder.

| Fresh candidate | Exact result | Why it is not a small existing-proof adapter |
|---|---|---|
| G2 o1/3011 m5186 | Loop0, header block7 (working PC156), exits to block39 (working PC227) and block33 (working PC517); block39 then reaches33 | The existing corridor pass retains39 as the following arm; 33 is a shared join. A new exit-frontier proof must select the common continuation while preserving every original edge/phi copy and nesting. Following a jump or replacing the exit ID would discard that obligation. |
| G9 o14/16446 m16784 | EnvironmentFlowError::kAmbiguousJoin at **original PC275 / working PC272**; one predecessor from working PC241 has local EnvironmentId0 (allocated working PC56), another has CapturedDepth0 | Equal numeric zero is not equal storage. Async normalization has already changed PCs. This needs activation/pre-throw/resume environment correspondence or a proof that a differing path is excluded, not an alias/index fix. |
| G10 o15/37098 m37881 | ControlCheck's normal zero-arc port has a pending branch stop (`control_verify.cc:147`) | A normal try-exit cannot be treated as return/throw. Continuation-bearing port/follow correspondence must be proved; weakening the verifier or marking the port abrupt is unsound. Original PC remains unlocated. |
| G25 o32/58223 m58418 | kPreludeEffects remains at original PC18's helper creation, before async entry PC25 | The rest-declaration certificate consumes an already-built ValueProgram and ValueEnvironments. This gate precedes those inputs. Reuse needs a common pre-normalization private-instantiation analysis; adding DEFINEFUNC to the existing no-closure prefix admission is not that proof. |

The other eligible groups retain the triage's cross-layer requirements: module
initialization (G7), method-bearing object buffers/home objects (G8), phased
static initialization (G16), instance-initializer callable role (G22), and phased
class creators (G26). None acquired a new independent proof in the delivered
literal/guard/cell work. Parked cases were not re-admitted or chased.

G1 is the largest eligible common group (two records). Its existing creator
order/storage evidence and shared class-preparation path make it the most
valuable next L track; its extra capture case is explicit, not hidden in a
special-case reader change. G18 is a possible later consumer, **not** a third
promised completion.

# Existing evidence and the actual gap

Original bytecode flow retained in [Round 76](round-76-static-self.md), with
symbolic roles below (these labels are not fabricated ValueIds):

```
o0: creator m4850
  PC6 allocate private class environment
  PC13 create class C
  PC28 store C into that environment's slot0
  PC40 create initializer method m5040
  PC51 invoke initializer with receiver C
  PC81 publish module export
m5040: PC10 load incoming depth0/slot0; PC12 HoleGuard
       PC18 construct from the guarded class value; PC22 own static-field definition

o20: creator m8392
  PC32 allocate class environment; PC39 create C; PC54 store self depth0/slot0
  PC250 create initializer m8556; PC261 invoke; PC271 pop; PC272 publish
m8556: self loads PC11/90, guards PC13/92; construction PC81/169;
       own field definitions PC85/173
```

Both self stores precede initialization. These are not early reads of the outer
publication binding. m8556 additionally reads two real creator cells at depth1
slots0/1; they remain live outer bindings. Its calls can throw and re-enter.
Cleanup handlers H58/H268 pop the private frame environment and rethrow.

Current call sites remain incompatible with that evidence:

- `family_file.cc` loads initializer witnesses before ClassSelfOrder::Build.
- `class_initializer_file.cc` chooses InputCaptureMode::kClosed.
- `class_self_order.cc` requires an empty initializer capture set and a
  ClosedClassFields certificate; that certificate excludes the relevant calls.
- `class_tree.cc` builds the initializer with IncomingCells::kReject, outside
  prepared member-family capture binding. The static-field printer also rejects
  incoming/self bindings; removing that check would not establish class scope.

The missing protocol is **one original private cell realized as a native
class-inner binding**, with creator reads after class completion using the real
retained class result. The outer export/local binding is distinct and stays
uninitialized until its own original publication point. Neither cell names nor
plan-local CellIds establish this correspondence.

# Proposed sealed proof and representation

Add `class_self_initialization.{h,cc}` with discovery plus
`VerifyClassSelfInitialization` over immutable original creator/initializer
inputs and complete family capture/write summaries. Seal a non-publicly
constructible `ClassSelfInitializationProof` only after independent re-derivation.

Its identity includes route/profile/contract/revisions, CreatorClassOccurrence
(family occurrence plus creation OpId), ClassInitializerCall, the real storage
(environment activation plus slot), the sole self-store, guarded read/commit
sites, class result and exact initializer receiver. Borrow original bodies and
pool indexes from the common preparation owner; do not decode per creation.

The seven obligations are:

1. **Occurrence/storage:** allocation, creation, store, receiver and incoming
   captures resolve to the same dynamic class occurrence and storage; copies and
   phis use existing identity/dominance facts. No assumed slot0 or CellId join.
2. **Initialization:** the class value commits before self initialization;
   self initialization dominates every admitted guarded use in this activation.
   Retain the original guard and its obligation. An early/hole-possible read
   stays refused; do not print an already-initialized inner name for it.
3. **Writes/re-entry:** the cell has exactly its initializing write across the
   creator and all transitive capturing descendants. Unknown/partial writers
   refuse. Re-entrant constructor/static/member calls and escaped read-only
   closures therefore keep the same value, including after initialization throws.
4. **Scope/name:** introduce a typed class-internal binding/scope, separate from
   FunctionSelf and creator Capture. Keep SymbolExpr references, with an explicit
   ClassSelfBindingRef capture target; never fabricate a function owner. Verify
   spelling resolution, shadowing and the original runtime `.name`/NamedEvaluation
   before allowing a printed inner class name. Otherwise refuse kName/kScope.
5. **Other captures:** forward m8556's non-self needs through ordinary family
   requirements and the actual creator CaptureStorage. Language needs dominate
   Guarded needs; never mark an ordinary outer cell initialized or snapshot it
   because the self binding is initialized. Preserve repeated reads and timing.
6. **Events/exceptions:** retain distinct internal-class availability,
   self-binding commit, initializer entry, ordered value/own-definition commits,
   successful class completion and outer publication. Every throwing event has
   its original handler/payload/normal Commit. Native class result availability
   cannot impersonate the earlier internal constructor value with a fake ExprId.
7. **Unwind/lifetime:** map cleanup POPLEXENV to class-scope unwind explicitly.
   It is not a transparent forwarded handler. Preserve surviving captured
   bindings of leaked constructors/closures, original payload and outer TDZ on
   failure; caller/physical catches stay checked per original entry.

Extend ClassTree/OriginalFlow/RecoveryFlow with typed class-initialization ports
and the class binding. The internal value/binding ports need no ordinary source
variable. After successful initialization, creator-side reads can use the
already-retained class result (with ordinary verified materialization if needed),
never evaluate the class again. Native static fields keep exact own-definition
semantics; no arbitrary assignment/IIFE/Object.defineProperty replacement.

Failures are fixed-size, located and append-only: kOccurrence, kStorage,
kInitialization, kWrite, kGuard, kScope, kName, kOrder, kCatchState,
kCorrespondence, kResourceLimit/kWorkLimit. Include creator/initializer, creation
and observing op/use/edge, original/working PCs and storage identities. Existing
kInternalValue/kOwner/kObservation/capture/escape refusals remain until this
sealed proof closes their specific obligation. No new language premise.

# Three gated increments

1. **Evidence/proof only.** Build the shared capture/storage/write index; derive
   and independently verify the class-self certificate. Read conditional inputs
   for evidence through existing APIs, without granting publication. Report both
   targets; first causes/status/text stay unchanged. Never catch a verifier error
   and retry with more permissive inputs. Deterministic discovery precedes sealing.
2. **Preparation/scope/events.** Reuse creator-class preparation, merge initializer
   family requirements before creator freeze, reserve full owner ranges and bind
   through real creator storage. Add class scope and port correspondence to
   TreeFlow/OriginalFlow/recovery; independently verify every projection. Keep a
   located recovery gate. Existing successful closed initializers retain their
   original preparation, names, owners and print path.
3. **Publication.** Remove that gate only for sealed complete occurrences, then
   allow the existing class/static-field printer to consume the verified binding.
   No inferred naming or capture flags at print time. Admit m5040 first; m8556
   publishes only when its real outer captures and all events also verify.

# Tests and admission cost

Real es2abc fixtures must first assert the two distinct bindings and actual
store/call order. Cover `class C { static x = new C(); }`, multiple static fields,
outer-vs-inner shadowing/TDZ, calls capturing/reassigning other creator locals,
re-entry through a static method, a leaked constructor followed by a throwing
initializer, repeated creations/loop activations and a throwing constructor.
Engine-output Node differential compares values, constructor identity, `.name`,
descriptors, an inherited static setter that must not run, constructor [[Set]]
behavior, exception constructors and full traces; no message/stack equality.

Mutations: wrong occurrence/environment/slot, substituted outer binding, a later
or missing self store, a descendant write, missing/doubled guard, swapped field
order, wrong receiver/capture demand, forged cleanup payload/port, early outer
publication, wrong class name and a stale owner/revision. Keep early-read,
unproved heritage/activation and partial-descendant cases as typed refusals.

Prepay shared indexes once per immutable preparation owner. Explicit stacks,
parent-linked occurrences, sorted storage joins, existing dominance/effect facts
and bottom-up summaries give O((V+E+C) log(V+C)) for admitted work, where C counts
actual capture/occurrence/output entries. No per-class whole-family scans or
recursive descendant walks. Borrowed buffers retain their owner's charge; every
new summary/port/stack is charged before allocation. Test 200/201/400/800 width,
depth and dense capture demand with exact byte/work boundaries and exhaustion.

Final acceptance: full build, serial source regression, new proof/mutation/budget
and fixture tests, class/capture/initializer/recovery plus CLI/manifest sweep,
node --check and both 40-input cohorts. Previously complete records' status,
source, names, file extensions and position maps must remain byte-identical.
No capability, implementation, Node differential or new proof test is claimed
by this design-only round.

# Current-round verification

Fresh baseline and current exports at **1d8326e8** are byte-identical:
**3,653 files / 532 complete** under previous flags and **3,809 files / 584
complete** under --language-source, each 620 module records / 40 inputs.
The full build and serial source-regression **65/65** pass. The
CLI/corpus/failure/receipt/manifest sweep is **78/79**; its sole failure,
`family_source_cli_test` (owner/origin arena, CMake line77), reproduces using
rebuilt wtbase **1d8326e8**. No expectations were changed.
No production behavior, new fixtures/tests, source contracts or ISA ledger
classes changed. Scratch probes/reports are under `build/review/next-choice/`.

# Implementation checkpoint: raw certificate only (2026-10-20)

**Increment 1 is implemented; increment 2 is not finished.** The new
`ClassSelfInitializationProof` and independent verifier join the actual creator
occurrence, environment allocation/slot, original class and initializer call,
complete constructor/member/initializer families, guarded reads and their
exception exits, read-only transitive captures, and original cleanup dispatch.
The verifier reconstructs literal/defined-member and descendant coverage; a
missing child cannot conceal a writer. Plan-local CellIds are consulted only
inside their owning plan. Cross-family correspondence resolves the original
creation environment and incoming address. All buffers are admitted before
allocation; each family is walked with flat indexed/sorted tables.

The production hook is failure-only evidence, after retaining the original
Closed refusal. It cannot return a plan, a ClassSelfOrder, a capture target or a
source receipt. `ResolveClassInitializerContext` factors the genuine occurrence
resolution out of the unchanged Closed reader, so conditional family evidence
loads the initializer once. No verifier failure is retried for admission.
Both manifest JSON paths and abc_semantic expose `class_self_initialization`;
status 1 denotes **raw certificate verified**, not source scope/recovery verified.

| Record / creator / initializer | Raw certificate | Guarded self reads across all families | Incoming cell records | Family roots | Cleanup |
|---|---|---:|---:|---:|---:|
| o0/4660 / m4850 / m5040 | verified | 5 | 4 | 18 | 1 |
| o20/8166 / m8392 / m8556 | verified | 2 | 3 | 10 | 1 |

The first refusals remain SemanticProofError::kInternalValue >
InternalValueReason 6 at m5040 PC12 and m8556 PC13. **No kRecoveryGate is emitted.**
Changing that cause now would misrepresent preparation completeness.

Specifically, increment 2 still needs the typed ClassSelfBindingRef/class-inner
scope in the real initializer family, ordinary forwarding/binding of non-self
requirements before the creator freezes, full owner ranges, and distinct
internal/self/initializer/completed/public ports through ClassSelfOrder and
ClassTree. It must also compose accessor-definition events with initialization:
m8392 installs eight accessor methods at PCs74–235 before its initializer call
at PC261. The current BuildClassLayout rejects definitions together with an
initializer, and ClassSelfOrder still requires empty captures/ClosedClassFields.
The raw writer census can prepare those member families separately; it grants
no permission to bypass either source placement check. Cleanup pop/rethrow must
then be consumed as class-scope unwind with its original payload/dispatch, not
as a transparent forwarding route. Joint recovery/publication remains increment 3.

## Increment 2 continuation: storage-bound scope view (partial, uncommitted)

`class_self_scope.{h,cc}` now consumes the raw certificate only after fresh
`VerifyClassSelfInitialization`. Its independently recomputed view contains:

- `ClassInnerBinding`: creator occurrence, original creation/allocation and slot.
  `ClassInnerReference` keeps its actual incoming address, owner occurrence and
  revisions, original read/value/guard and exception destination. Indexed lookup
  rejects unlisted owners and stale revisions; no name or cross-plan CellId join.
- Complete relative owner ranges, including forwarding-only descendants, and
  occurrence/address-bound requirements. Self requirements and ordinary outer
  requirements are distinct. The existing capture-domain lattice is preserved;
  m8556's outer Language requirements cannot become initialized self references.
  Relative ranges are preparation reservations, **not** fabricated syntax owners.
- Separate original class-availability, internal-store Commit, initializer
  Before/Commit and public-store Commit ports. Original self loads in the creator
  are available only after the initializer. Original cleanup entries and selected
  initializer dispatch are retained, never treated as transparent forwarding.
- A private member-installation corridor. Shared effect predecessor/successor
  identities prove every installation completes before the initializer and that
  no unrelated observer is hidden between the internal store and invocation.
  Fixed class-prototype reads reuse `FindClassPrototypeReads` and resolved
  `HasClassPrototypeKey`; an arbitrary property read cannot enter the corridor.

The production change is still failure-only diagnostics: optional `scope_view`
counts/status/failure in the existing diagnostic, on both manifest JSON paths,
abc_semantic and receipt round trips. A verified scope **view** is not a prepared
ClassTree. The original Closed refusal remains; no `kRecoveryGate` is fabricated.

Real-input results (baseline `19b5b561`): m4850 has 23 owner occurrences,
5 references, 2 inner/2 ordinary outer requirements and **zero direct class-valued
public stores**. Its PC81 export consumes the getInstance result, not the class.
The class result retained after PC51 therefore needs its own completed-class
availability correspondence; treating that export as the class publication would
be wrong. m8392 has 10 owners, 2 references, 1 inner/2 outer requirements and one
direct public commit. Its accessor-installation corridor verifies. These counts
are diagnostics, not substitution of either original value or source binding.

### Accessors before initialization

Pinned `ets_runtime/ecmascript/interpreter/interpreter-inl.cpp:2627–2645`
passes the actual object/key/getter/setter/flag to the runtime and commits the
returned object only on success. `ecmascript/stubs/runtime_stubs-inl.h:2708–2775`
performs ToPropertyKey, optionally names the functions, constructs a configurable,
non-enumerable accessor descriptor on class/prototype targets and calls
DefineOwnProperty. It does **not** invoke the getter or setter. The stub bridge is
`runtime_stubs.cpp:2896–2907`. Thus constant keys, fresh original home objects,
unchanged descriptor/name semantics and a private must-reach installation corridor
can preserve this order in a source class body; moving an installation across an
unknown call is not justified.

The pinned dynamic frontend agrees: `ets_frontend/es2panda/ir/base/classDefinition.cpp:
410–422` initializes the class name, compiles missing properties, then calls the
static initializer. The new real `accessors.js/.abc` fixture exercises accessors
and self-reading static initialization together. This closes the **ordering**
question; the current ClassTree's mixed-definition/initializer gate is deliberately
not removed before its source binding/enclosure checks exist.

### Remaining admission boundary

This continuation is **not the requested complete increment 2**. The view is not
yet a new TreeCapture target or a native class-inner Scope/SymbolExpr in the actual
initializer tree. `FamilyPreparation` must consume its requirements before creator
freeze, reserve the real full owner ranges and bind non-self cells through the real
creator Storage. Then ClassSelfOrder/ProjectClassMembers must consume the certified
private corridor, and ClassTree must finish the initializer family with the typed
binding, separate ports and cleanup-unwind correspondence. Native class-name
spelling/shadowing and real `.name` remain source obligations. Only after those
checks can an immediately-pre-publication recovery gate truthfully replace the
current internal-value failure. The Closed reader and all successful paths remain
unchanged in this continuation.

### Continuation acceptance at 19b5b561

Full build passes; serial source-regression **65/65**; focused tests **8/8**.
The class/static/initializer/family/capture/receipt/manifest sweep is **86/94**.
All eight failures reproduce using wtbase `19b5b561`: family_source_cli,
class_tree_source, captured_receiver, unguarded_captured_receiver,
reversed_captured_receiver, unguarded_reversed_captured_receiver,
reversed_initialization_captured_receiver and its unguarded variant (all `_test`
source-test names as listed in status.md). No expected output was relaxed.

The new scope view's exact 200/201/400/800 byte boundaries are
99,150 / 99,468 / 183,230 / 351,390; work boundaries are
60,194 / 60,474 / 116,482 / 229,858. Build and verifier work exhaustion, one-byte
below admission and the ownership/storage/order/cleanup mutations all refuse.

Fresh cohort exports and `cmp_cohort.py` retain **532/620** previous flags and
**584/620** source. Complete sets, every first cause and all non-manifest files
are byte-identical (3,653 / 3,809 files). Only the two target manifests add
`scope_view`; receipt and both JSON-path round trips pass. abc_semantic reports
the same first refusals at m5040 PC12 and m8556 PC13 with verified view counts.
Target CLI peak RSS is 13,484,032 / 9,256,960 bytes; this is target-probe evidence,
not an aggregate cohort RSS claim. Logs and exact comparison:
`build/review/class-self2/`. No kRecoveryGate or engine-output acceptance yet.

Fixtures and commands are in
`tests/integration/fixtures/class_self_initialization/README.md`. They check the
raw certificate and continued Closed refusal, not the future recovery gate.

## Increment 3 consumption checkpoint (2026-10-20, baseline 9d77c21b)

The verified scope is now consumed by the **common FunctionTree builder**.
`BuildClassSelfClassDraft` freshly verifies the occurrence/storage certificate,
merges root family demands into the real creator's CellPlan before creator
freeze, and uses the owner ranges already allocated by class preparation.
`ClassSelfOrder::BindInitialization` binds the original private self store;
ClassTree retains separate internal/public ports, selected initializer dispatch
and the exact cleanup entries. Accessor installation uses the certified corridor
above; it is not admitted by opcode adjacency or by deleting the old mixed-class
check. The successful Closed path is unchanged.

The initializer gets a real `kClassInner` scope between its parameter scope and
its original outer capture/module scopes. Incoming cells are joined by verified
occurrence and original environment/address; only certified self requirements
become immutable `kClassInner` SymbolExprs with the native class spelling. Locals
reserve that spelling. Ordinary outer requirements remain typed, address/domain-
bound `kCaptureRequirement`s, **not** fabricated CapturedSymbol aliases or
initialized external bindings. Real module aliases use the existing ModuleAliases
path. No method entity, invented owner or plan-local CellId establishes a
cross-family target.

`VerifyClassSelfClassDraft` independently re-verifies the original certificate,
owner ranges, ports/cleanup, binding plan, scope/name resolution, and exactly one
source reference for each original initializer self read/guard. It summarizes
original-site DAGs once; sorted address joins are O(log N) each. Buffers and
nested analysis costs are charged before allocation; resource/work exhaustion
refuses. A separate conditional structure verifier checks draft syntax; ordinary
VerifyTreeStructure and recovery reject class-inner/pending-capture drafts.
It is never a ConstructedFunction and cannot reach the printer.

**Publication is still closed, for specific projection gaps rather than an
internal-value leak.** m8556 now has two real class-inner expressions and ten
allocated family owners. Its two ordinary outer requirements still need the
real creator CaptureTree enclosure, including native spellings/TDZ domains;
its first refusal is ClassSelfError::kRecoveryGate with
`Projection::kCaptureEnclosure` at m8556 PC13. Joining those requirements to
creator demand storage is not that enclosure proof. After that come joint
OriginalFlow/recovery of the class-initialization ports, public-binding lifetime,
and completion/cleanup on each projection. Those are deliberately not assumed.

m5040 has one initializer-tree self expression (five across the raw family
inventory) and 23 allocated owners. Its first gate is
`kClassInitializationPorts` at PC12; its retained completed-class availability,
additional member-family enclosure and getInstance/export correspondence remain
unverified. Its export must not be relabelled as the class publication.

All seven fixtures build and independently check the initializer draft, and
assert a typed recovery gate: `outer` needs capture enclosure; self/throwing/
reentry/branches/copies/accessors first need class-initialization recovery ports.
No emitted class is offered as semantically accepted output. Consequently Node
and es2abc equivalence on *newly published* class text are not claimed this round.

### Consumption acceptance

Full build passes; focused tests **9/9** (including the real record exporter),
serial source-regression **65/65**. The requested sweep passes **87/95**; the
other eight failures reproduce at **9d77c21b**: family_source_cli, class_tree_source
and the six captured-receiver variants listed in status.md. A first run exposed
that the new exporter test reused a directory the CLI intentionally refuses to
overwrite. Its per-run temporary output fixes that test harness issue; repeat
runs and the full sweep retain all semantic gate assertions.

Both fresh cohorts and exact directory comparisons retain **532/620 / 584/620**,
with no status or source-text changes, and all 3,613 / 3,769 non-manifest files
identical. Only o0/4660 and o20/8166 diagnostics move to the stated typed gates;
abc_semantic agrees with the manifests. Exact 200/201/400/800 draft bytes are
2,347,215 / 2,358,103 / 4,606,735 / 9,125,775; work is 1,445,501 / 1,452,838 /
2,864,716 / 5,708,978. Scratch logs: `build/review/class-self3/`.

## Increment 4 checkpoint: real creator enclosure consumed (2026-10-20)

**Partial delivery, with publication still closed.** The two ordinary outer
requirements of m8556 now bind through m8392's actual creator CaptureTree.
`ClassSelfDeferred` retains the sealed raw occurrence/scope evidence until the
existing two-phase class preparation has constructed that tree. The reader's
original Closed refusal remains the deterministic entry condition. Existing
successful preparations take their unchanged path.

`ClassSelfOrder` carries the verified private-installation corridor. Definition
and initializer projection use that evidence to retain the original private self
store as an ordinary cell operation, while projecting only the already-certified
class installation events. It does not move the public store or initializer.
The actual creator class task reserves the initializer's syntax owner from its
full owner range. Binding is performed there, before finishing the class.

`BindDeferredClassSelf` uses BindCreation, BindCaptureCells and the independent
capture verifier with the real creator storage. The initializer's incoming
(depth, slot) is joined to the actual parent cell, then to the original-flow
storage/environment and source symbol. No plan-local CellId crosses the join.
Aliases use the creator's final spelling, preserve the original capture domain,
and remain plain lexical references; no outer value is read or copied early.
VerifyDeferredCaptureTree must still prove declaration-at-store fulfilment.
Consequently a late outer declaration without that proof remains
`kCaptureEnclosure`; the new `outer_tdz` fixture checks this negative case.

`VerifyClassSelfEnclosure` independently reconstructs those targets, re-verifies
the raw/scope certificate, checks aliases/domains/names and original occurrence,
and runs the existing class draft and enclosing structure verifiers. ClassTree
retains the original, distinct internal/public ports and cleanup entries; its
outer targets are checked at the real ClassExpr. Name/storage/cleanup/port
mutations must reject. The retained family snapshots are for raw analysis only:
their early borrowed module-parent inputs are not used after creator movement
and must never be sent to PreparedFamily::Finish. Any future finishing must use
the freshly prepared family under the actual creator, as the ordinary path does.

**Remaining projection:** common VerifyTreeSemantics still rejects the new
class-inner scope. OriginalFlow/RecoveryFlow also lack a joint, cross-method
class-initialization port relation: private self Commit, initializer Before and
Commit/exception, then public Commit with the outer binding's original TDZ
lifetime. Treating a ClassExpr Commit as all these events would be unsound. The
new path attempts the common verifier and preserves a located
`kRecoveryGate / kClassInitializationPorts` at m8392 op100 PC261, with
`bound_outer: 2`. Neither ordinary recovery nor the printer admits this draft.
This gate does not claim that joint recovery or native output equivalence has
passed. o0/4660 remains unchanged at its previous gate; its separate completed-
class availability and additional member-family correspondence are not attempted.

All eight real fixtures explicitly assert publication refusal: self, outer,
throwing, reentry, branches, copies and accessors need initialization ports;
outer_tdz needs capture enclosure. The common production exporter is exercised,
as well as the independently checked creator/enclosure path. Width 200/201/400/
800 is tested both with self-only fields and with a real outer requirement;
exact work/minimum-byte admission and one-below resource refusal are checked.
No newly published class text exists this round, so no engine-output Node check
or es2abc/ark_disasm equivalence result is claimed. The new fixture's compiler,
command and hash are in its README; tests do not execute the original source as
if it were decompiler output.

### Increment 4 acceptance

Full build passes. Final sequential acceptance is **65/65 source-regression**,
**10/10 focused tests**, and **207/221** in the requested tree/scope/capture/class/
static/initializer/family/receipt/manifest/CLI/binding/cell sweep. All 14 failures
reproduce with rebuilt 55aab71f executables (family_source_cli, tree_structure,
try_tree and source, derived_cells and source/unguarded, class_tree_source, six
captured-receiver variants). Their logs and the exact test-name comparison are
under `build/review/class-self4/`; none is relabelled as a new passing test.

Fresh cohorts and both cmp_cohort/exact directory comparisons retain **532/620**
and **584/620**. All 3,613 / 3,769 non-manifest files are byte-identical; only
m8392's o20/8166 diagnostic changes. o0 and every other manifest are unchanged.
Independent enclosure minimum bytes at 200/201/400/800 are 2,450,621 / 2,462,314 /
4,830,181 / 9,589,301; exact work is 1,093,601 / 1,100,998 / 2,174,665 / 4,339,193.
The new bound evidence is checked through receipts and both JSON paths, including
invalid counts and missing-recovery rejection. The test-only receipt includes its
actual working revision; the scale harness isolates each invocation's generated
ABC files. Final runs are sequential, avoiding overlapping fixture producers.

## Increment 5 checkpoint: real scope and class-initialization ports (2026-10-20)

The three-port design is used, with StaticInitialize split into Before/Commit.
`ClassInnerScopeProof` freshly re-derives and independently verifies the original
scope certificate before sealing it. The common structure, cell, TreeFlow and
RecoveryFlow paths accept `kClassInner` only with that seal. A native inner read
retains the certified read/guard provenance; TreeFlow and operand verification
still reconstruct the physical guarded read and its exception correspondence.
The immutable binding receives a distinct `kClassInner` entry definition, never
an ordinary capture or an invented initialized public cell.

`class_initialization_ports.{h,cc}` retains the original self-store, initializer
Before/Commit, public writes and cleanup route. Recovery produces allocation,
inner initialization, static Before and static Commit as distinct points; its
original-action table maps those typed ports without fabricated ExprIds. Each
throw bypasses static Commit and public publication. Original physical public
write definitions remain separate. Every recovery projection independently
checks the source/raw role census, original write identity, normal/throw edges,
Commit dominance and exact retained cleanup/dispatch certificate. The current
adapter admits only the proven caller exception destination; an enclosing catch
or finally needs its own verified correspondence.

The original public-read census uses one shared effect/dominance index and a
sorted binding index. Every original read of a publication binding must precede
creation or follow its original public Commit. Native declaration/TDZ and source
write lifetime remain under the existing binding/initialization verifier; the
port table cannot authorize hoisting a declaration or fabricating initialization.
All buffers and origin/port indexes are prepaid. There is no per-class CFG walk:
work is linear plus shared sorting/dominance, O((V+E) log V).

### Safe publication boundary

Root-only certified initializers can finish through the common creator/family
path. Named class expressions are required so a native static field/block sees
the inner name, not a later creator alias. The existing printed ClassNames proof
must independently permit the name; it cannot be forced across a shadowed
capture/global. Native field definitions retain DefineOwnProperty; an ordinary
static statement uses `static { ... }`, retaining its Set semantics. Mixed
field/block representations without a proof remain refused. Accessor installation
ordering is still the private corridor proof described above, not a new blanket
admission.

A member/descendant reading the private self cell is explicitly **not** covered
by an initializer-only projection. In particular, printing a re-entrant method
against a creator cache initialized after the class would be wrong. `reentry`,
`accessors`, `copies` and the closure-bearing `throwing` fixture now retain
`kRecoveryGate / kFamilyEnclosure`; `outer_tdz` retains capture enclosure. The
simple self, outer and branch fixtures publish. `constructor_throw` publishes
and exercises cleanup: the constructor trace occurs once, its RangeError escapes,
and no public store completes. Node tests compare actual engine output with
independent original source, including inherited Function.prototype setters:
fields do not invoke them, while the ordinary static assignment does.

### Target evidence and remaining work

**o20/8166 is not published.** m8556 now passes common initializer scope/semantic
verification with two real outer requirements bound. Finishing the actual class
next reaches constructor **m8638**, `FunctionAbiError::kLength`: native length
**1**, recovered signature length **7**. This is wrapped as the precise
`kRecoveryGate / kMemberAbi`, with `abi_lengths` round-tripped through receipts
and both JSON paths. Its default-parameter proof is a separate next task; no
length check or signature semantics is relaxed. The full creator publication
projection for this target cannot be claimed while its constructor remains
unverified. o0/4660 stays at m5040 PC12, class-initialization/completed-class
availability gate; no getInstance admission was added.

### Recompilation evidence

`class_self_ports_roundtrip.py` reuses grouping_bytecode.py's compiler identity,
invocation and exact disassembly reader (no register/branch/handler normalization).
Four published fixture exports are recompiled at default and O0. SDK identifier
export aliases are unquoted on both sides; source binding/export identities are
unchanged. Compiler SHA-256 is
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`;
options are `--module --merge-abc --extension=js --source-file=input.js`, matching
record names, plus `--opt-level=0` in O0. Commands and full diffs are retained.

Exact full-disassembly result: **1 identical / 7 different / 0 compile failures**.
These are semantic recovery differences, not grouping acceptance passes:

- Default self/branches have identical method instructions/handlers; physical
  string/literal/module-table offsets move with the changed source container.
  Default constructor_throw is entirely identical.
- Default outer additionally changes only the HoleGuard's diagnostic name from
  `outer` to the real recovered spelling `$local1`. Storage depth/slot, guard,
  exception constructor and order stay identical. The fixture initializes that
  binding to 7 before class creation, so the diagnostic is unreachable. Original binding names were
  already synthesized by the existing recovery policy.
- At O0, constructors and initializer instruction bodies remain identical except
  that outer guard spelling. Creator register assignments and private class-result
  aliases differ: named-expression evaluation retains the class in temporary
  registers, initializes its internal cell, calls the same initializer/receiver,
  performs the same pop/rethrow cleanup, then writes private source aliases and
  the same export. These extra private aliases have no earlier observer; the
  original result/port/capture proof checks their value and timing. No handler,
  call, public commit or field operation is normalized away.

Both sides of all four O0 comparisons pass fresh raw certificate, scope, guard,
cleanup and joint-port mutation/exact-budget tests (**8/8**). Default optimized
bytecode on both sides (**8**) remains **unverified by that raw proof** with
`CleanupError::kHandler` (serialized value 1): its handler saves/reloads the pending
exception around pop. Equality of compared default methods is not misreported
as proof that this unimplemented cleanup adapter passes. No target o20 bytecode
comparison is claimed because it has no published output.

### Private materialization and final projection check

Ordinary recovery's source-cache action can begin at evaluation of its RHS.
That action is not the raw self-store Commit. The typed port replaces that
correspondence only for the independently certified internal initialization.
A shared original-operation index then rechecks every retained cache definition:
its original write site, stored SSA value, source binding and private unit must
match, and static Commit must dominate the materialization. An externally visible
cache or a materialization before initialization refuses. Source value/provenance
verification still owns every copied result; no fabricated expression is inserted.

The unit path now finishes the actual constructor/member families before deriving
closed-cell facts. A bare metadata layout correctly marks its capture effects
unknown and cannot certify this projection. Mutations include an exposed cache,
a moved cache definition, early public publication, forged ports and missing
exception edges. Inner-scope and port verification each check exact budgets and
one-below refusals, in addition to the existing raw certificate/enclosure tests.

At default optimization, **11/12 methods** compare exactly; at O0, **7/12**.
The six changed method/mode pairs are the four O0 creator methods and the outer
initializer in both modes, with the differences enumerated above. The O0 original
side is separately checked against the actual fixture ABC's disassembled methods.
The independent original JS is only a test/compiler oracle, never an engine input.

### Increment 5 acceptance

Full build passes. Final serial source-regression is **65/65**, focused tests
**12/12**, and the requested sweep **209/223**. The 14 failures exactly match the
user's pre-existing list and the recorded baseline replay; the comparison is in
`build/review/class-self5/preexisting-failure-comparison.json`. Test expectations
now assert o20's constructor ABI gate through manifest and abc_semantic. There is
no claim that o20's full class publication projection passed.

Both current flag sets were freshly compared with **1a7da988**: **532/620** and
**584/620**, no regression or new completion, every complete source byte and all
3,613 / 3,769 non-manifest files identical. Only o20/8166's diagnostic changes.
Final measured per-engine peak RSS (all 80 invocations, threads included) is
42,598,400 / 42,106,880 bytes using Darwin RUSAGE_CHILDREN from fresh wrappers;
all engine exits are zero. The sandbox's `/usr/bin/time -l` attempt could not read
kern.clockrate, so its 40 nonzero wrapper statuses are not engine failures or RSS
evidence; the later direct rusage runs supply the measurements and exact outputs.

Standalone class printing must still supply an ordinary initializer's creator
captures; a certified inner binding cannot silently fall back to an anonymous
class form. The normal ClassExpr path uses its real creator and the existing
name plan, source/recovery ports and actual capture targets. The new negative
unit check exercises detached outer-capture refusal.

## Increment 6 checkpoint: member ABI diagnosis (2026-10-20)

Baseline: `024e759c`, including `build/review/wtbase/b/abc_structure`.
This checkpoint does **not** implement expression-default recovery or publish
another class. The independent negative regression preserves the real ABI gate.

### m8638: defaults are present, but their expression proof is missing

The original creation in m8392 PC39 records constructor length **1**. m8638 has
**seven** visible frame parameters; the callable/receiver ABI entries are not
visible parameters. The first is required. Its original prologue is:

| Visible parameter | Undefined test / branch PC | Missing-path initializer PC | Value |
| --- | --- | --- | --- |
| 2 | 31 / 34 | 36, 38, 46 | live external module slot 1, HoleGuard `SizeMode`, then `Normal` property read |
| 3 | 57 / 60 | 62 | integer 0 |
| 4 | 74 / 77 | 79 | integer 0 |
| 5 | 91 / 94 | 96, 105 | integer 1, numeric negation (-1) |
| 6 | 114 / 117 | 119 | integer 1 |
| 7 | 131 / 134 | 136, 144 | global lookup `VerticalAlign`, then `Center` property read |

These are actual branch/phi defaults, not rest, destructuring, an `arguments`
tail or seven required parameters. In particular, the first initializer is a
**module** binding plus property access, not a direct captured lexical-cell
initializer. There is no fresh-object default in this constructor. The raw
value graph (IDs from the production reader, not paired source) contains:

```
op13 PC31 B0: StrictEqual(entry v16 [parameter index 1], undefined) -> v41
op14 PC34 B0: Branch(v41), Use15
op15 PC36 B1: ModuleRead(external slot 1) -> v100
op16 PC38 B1: HoleGuard(v100), Use16
op19 PC46 B1: NamedRead(base v100, pool "Normal") -> v103, Use19
phi v22 B2: provided entry v16 | missing result v103
op69 PC167 B12: NamedWrite(this v14, stored v22), Uses74/75
...
op57 PC136 B11: GlobalRead("VerticalAlign") -> v84
op60 PC144 B11: NamedRead(base v84, pool "Center") -> v87, Use64
phi v27 B12: provided entry v21 | missing result v87
op73..89 PC178..222: remaining field stores
```

The module load, guard and property read retain their individual original
possible-throw exits to the caller. The property reads may execute getters;
neither can be replaced by its base, a cached value or an invented constant.
The body field stores occur only after the full default chain.

`FindDefaultCandidates` reaches the first missing arm with three live value
operations. `default_candidates.cc`'s `arm_facts.value_operations >
kLiteralAndNegation` branch returns not-applicable. `Finder::Find` then stops
at the **first** uncertified parameter; it does not migrate later constants out
of order. `CaptureBoundary` cannot certify this arm either: it is not an incoming
lexical read whose result is the fallback. Consequently the seven body-owned
parameters have no signature defaults and their source `.length` is 7.

This is not only a discovery limit that can safely be removed:

- `DefaultValueProof::CheckCandidate` proves constants or one direct incoming
  cell read/guard; it has no property-expression event owner.
- `DefaultBody` and `BuildDefaultSignature` build literal or capture-reference
  roots. `VerifyDefaultSignature` independently accepts those same roots.
- `VerifyFunctionAbi` permits only literal/default capture expressions and
  correctly rejects source length 7 against original length 1.
- The original/recovery conditional parameter chain in `parameter_recovery.cc`
  is bound to `CellParameterDefaultProof`. No current certificate owns this
  module load, its guard, the getter, the global lookup and their exception
  exits in parameter scope. Merely printing a default would bypass that joint
  correspondence and could move a throw into the body catch.

A native signature with the six defaults is the intended result. It needs a
**generic parameter-initializer expression proof**, not a class-self exception
or a new source premise. The narrow next implementation should reuse the
ordinary expression evaluator for a certified conditional initializer forest:

1. Derive argument-entry / missing decision / provided and missing commit /
   initialized-parameter identities from SSA and original edges. A shared
   def-use/control/effect index proves conditional ownership, unique consumption,
   left-to-right parameter order and no body/local/receiver dependency that the
   parameter scope cannot resolve. Keep existing literal/cell paths unchanged.
2. Retain each initializer's actual expression events and original exception
   exits. Resolve live module/global names through the real binding domain;
   preserve the HoleGuard and getter timing. Seal and independently rederive
   before removing those operations from the residual body.
3. Build the signature expression from that owner and verify its tree, ABI and
   OriginalFlow/RecoveryFlow parameter-entry chain on every projection. Missing
   input/guard/order/catch/ownership correspondence remains a typed refusal.
   No synthetic `= undefined` solely to reduce `.length` is valid.

This crosses discovery, signature ownership, module/name binding, ABI and
recovery; it is not justified by increasing the old arm-operation limit.

### Regressions and remaining completed-class port

`tests/integration/fixtures/member_expression_defaults/defaults.abc` is a new
real es2abc fixture with the same generic parameter obligations and a native
static self reference. It reaches `kRecoveryGate / kMemberAbi` with **1 / 7**;
the test asserts that outcome, not source recovery. The production-graph probe
also reproduces the empty candidate set on m8638. `default_candidates_test`
rejects a property access passed off as a constant, even with falsified pure
effects. No decoder, lifter, source or recovery production behavior changes.

For o0, the existing independently verified scope inventory still has **zero
class-valued public stores**. m4850 retains the class after PC51, reads/calls
`getInstance` at PC78, and exports that **result** at PC81. The current port
verifier requires a real PublicPublish. Removing that requirement would leave
completed-class availability and the saved class reference unmodelled. A
separate ClassCompleted continuation must tie static Commit to the retained
class value before the member read/call; the export remains a different result
identity. This checkpoint keeps `kClassInitializationPorts` at m5040 PC12 and
does not forge a publication port from the export.


### Increment 6 validation

- Full `cmake --build build/regression -j 6`: passed.
- `ctest --preset source-regression -j 1`: **65/65**.
- New/boundary tests: **3/3**. Class-self fixture suite: **12/12** in the
  requested broader sweep, including existing engine-output Node differentials,
  SDK round trips and 200/201/400/800 exact-budget tests.
- Requested tree/scope/capture/class/static/initializer/family/receipt/manifest/
  CLI/binding/cell/parameter/default sweep: **219/233**. The 14 failures exactly
  match the user-supplied pre-existing list; no failure was relabelled or fixed.
- Fresh old/new 40-input cohorts against wtbase `024e759c`: **532/620** previous
  flags and **584/620** source, no process failures. All 3,653 / 3,809 output
  files, including manifests, are byte-identical. No new target text exists;
  target Node differential and target original-vs-recompiled equivalence are
  **not performed**, rather than counted as passes.
- `build/review/class-self6/` retains raw m8638 IR, the actual disassembly,
  compiler/fixture identity, build/test logs, target first-refusal comparison and
  full cohort comparison. `tree_failure.mjs` needs no expectation change.
