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
