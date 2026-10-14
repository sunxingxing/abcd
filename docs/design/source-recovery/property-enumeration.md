# Native property enumeration

The approved target is native `for (key in object)` with no new source-language
premise. Steps A-C retain raw semantics, protocol and exception-forwarding
proofs; Increment II adds the production draft and independent TreeFlow checks.
Increment III below adds joint original/recovery event correspondence before
publication. Captured per-iteration bindings, handled advance and ambiguous
exhaustion copies remain refused.

## Raw operations implemented

`PropertyIterator` reads the accumulator and produces a distinct internal
`PropertyEnumerator` value. `NextPropertyName` reads its enumerator operand and
produces a language value. Neither operation is pure: heap reads/writes,
allocation and user code are conservatively retained both on success and before
throw. The accumulator is defined only on successful completion; the phased
import attaches that definition to Commit.

The pinned references are `OpenHarmony-v7.0-Release`, as recorded in
`docs/reference/openharmony-v7.0-release.lock.json`:

- `ets_runtime/ecmascript/interpreter/interpreter-inl.cpp`: GETPROPITERATOR at
  1797 and GETNEXTPROPNAME at 2574 check abrupt completion before updating acc.
- `ets_runtime/ecmascript/js_object.cpp:3006`: EnumerateObjectProperties.
- `ets_runtime/ecmascript/js_for_in_iterator.cpp:55`: mutation/deletion checks;
  `:84`: NextInternal and its undefined exhaustion result.
- `ets_runtime/ecmascript/property_accessor.cpp:388`: property/prototype key
  collection, including proxy handling.
- `ets_frontend/ets2panda/compiler/core/JSCompiler.cpp:1362`: native for-in
  lowering uses acquisition, advance and the undefined test.

The new internal domain may pass through ordinary register copies or the
enumerator input of `NextPropertyName`. It is excluded from language values,
Hole transport, source phi copies, returns and observable operands. Next is
deliberately not classified as String-only: the pinned proxy slow path does
not establish that refinement. Import does not prove closed use, loop
ownership, dynamic acquisition identity or source variable lifetime.

## Approved stages after raw lifting

The source route still refuses `ValueEnumeration`. Stages 1–2 below have raw
query implementations in B/C; stages 3–4, including the native binding and
source oracle, remain required before emitting any native for-in:

1. An indexed protocol proof over SSA and original control: dynamic acquisition
   identity, copy/phi identity, closed enumerator use, single advance per
   iteration, done equivalence on the next-result domain, key binding/live-out,
   and acquisition-to-first-next effect order.
2. A general exception-forwarding summary, consumed only by this projection.
   It must prove the thrown value, dispatch and observable state from the graph;
   STA/LDA/THROW adjacency is not evidence. Acquisition-only handlers must not
   be expanded to cover loop advancement or body effects.
3. Native ForIn tree representation and activation-aware key binding, with
   independent tree/recovery checks of event membership, effects, exceptions,
   original edges and binding activation. The two earlier MockKit index/delete
   loops must retain their existing plans.
4. Protocol-specific typed refusals and the real es2abc/Node integration
   fixture, including forged proofs, escapes, wrong completion, binding
   lifetime, and control/exception mutations.

Proof construction must allocate no source symbols or names, charge all
buffers before allocation, and remain within O(N log N). Existing complete
methods and their catches must not be replanned.

## Runtime verification boundary

The premise-free projection targets the pinned Ark runtime: es2abc lowers
native for-in to these same operations. Node differentials can check ordinary
objects, ordinary key order, control/return traces and exception constructors.
They do **not** establish equality with Ark for Proxy trap sequences, prototype
or enumerability mutation during enumeration, or the Proxy Symbol-key path.
Those cases remain an explicit runtime-verification gap; a source-only oracle
must not be reported as Ark execution evidence. No Node differential of
engine-generated for-in has been completed by this working increment.

## Validation of the raw increment

- `property_enumeration_test`: normal and phased imports, success-only acc
  definition, acquisition-only handler, internal-value escape rejection,
  register/copy/block-split variants, operand/effect mutations, and importer
  memory/work boundaries at 200/201/400/800 operations.
- Fixed source regression: 65/65. Selected loop/exception/iterator/source
  tests: 24/24. Logs: `/private/tmp/r51-final-fixed.log` and
  `/private/tmp/r51-final-targeted.log`.
- Full keep-going build succeeded; log: `/private/tmp/r51-final-build.log`.
- The targeted set was exception_plan, loop_exit, exception_copies,
  loop_syntax, loop_transition, loop_initializer, loop_closure_file,
  loop_forest, value_import, source_route, property_enumeration,
  exception_tree_source, exception_outer_source, loop_condition_source,
  loop_transition_source, loop_initializer_source, loop_exit_source,
  loop_closure_source, register_value_flow, exception_arguments,
  phased_value_import, iterator, exception_dispatch and exceptions_cli
  (all names have `_test`). No failure occurred in this selected set; the
  separately registered pre-existing failures were not changed.
- Both exploration comparisons against explore50/explore50s: 497/620 and
  534/620, zero status changes and zero source-byte changes. These unchanged
  counts are regression evidence only, not for-in completion.
- Comparison outputs: `/private/tmp/r51-final-cohort/explore51{,s}`; comparison log:
  `/private/tmp/r51-final-cohort.log`.

Both flag sets publish the same current first refusals:

| Record | Method | Original/working PC | Current refusal |
| --- | --- | --- | --- |
| o32/29268 assertContain | 29450 | 90/90 | EvaluationError::kUnsupportedOperation > EvaluationObligation::kPlan |
| o32/42397 DataDriver | 42717 | 153/153 | PhasedMethodError::kInput > SemanticError::kUnsupportedInstruction (GETITERATOR) |
| o32/43537 Filter, first loop | 44469 | 28/28 | EvaluationError::kUnsupportedOperation > EvaluationObligation::kPlan |
| o32/43537 Filter, second loop | 44469 | 179/179 | Raw operation lifted; source projection not independently reached past the first loop refusal |
| o32/21089 core | 21553 | 47/47 | EvaluationError::kUnsupportedOperation > EvaluationObligation::kPlan |
| o32/53454 MockKit | 54047 | 230/230 | EvaluationError::kUnsupportedOperation > EvaluationObligation::kPlan |

DataDriver's new first refusal is an independent iterator protocol and remains
out of scope. Its change of first refusal does not prove that its for-in source
projection succeeded: family preparation discovers another child before tree
emission. None of the five records is complete in this working increment.

## B/C raw proof increment (uncommitted)

`ForInPlan` is now a query over an already verified ValueProgram; it is not
called by source planning yet. It retains acquisition, advance, decision,
object UseId, enumerator/key identities, original body/done edges, key uses and
acquisition-handler evidence. No symbols, names, declarations, loop layouts or
existing catches are changed. B/C adds the following proof, not native source completion.

- Reuse `AnalyzeValueIdentities`' copy/phi SCC equations. A next input must
  identify one acquisition; acquisition success must dominate the loop header
  in the full exceptional graph and lie outside that natural loop. Normal loop
  discovery is separate from exceptional dominance. This distinguishes a
  static SSA name from the acquisition of the current activation.
- Closed-use and liveness indexes permit copy/phi transport, reject enumerator
  observations, and retain real key uses. A key use must be dominated by the
  successful body edge; a mixed/live-out phi is not silently given a native
  loop binding. Native binding activation remains a separate D obligation.
- Universal first-event summaries establish acquisition order and the next at
  the loop header. The done decision must precede each backedge. Unresolved
  normal cycles do not acquire a finite first-event summary. Before/Commit
  identity, rather than instruction adjacency, locates the branch edges.
- Abstract interpretation on the next-result domain proves the done test.
  Undefined is exhaustion; keys may be String (including empty string) or
  Symbol. Nullish equality, boolean conversion and negation are evaluated on
  that domain; key truthiness alone is not a done test. The primitive comparison
  proof does not license erasing an arbitrary coercing equality elsewhere.
- `AnalyzeExceptionForwarding` computes universal normal-path summaries by
  reverse Kahn propagation. All paths must terminate by throwing the same SSA
  payload to the same original destination without observable effects. Pure
  branches/joins and copies are allowed; returns, divergent paths, changed
  payload/dispatch and observable work have no forwarding summary. ForInPlan
  currently uses only a handler forwarding its caught payload to the caller.
  A handled advance remains `kCatchState`; nested catch staging is not added.

The original verified graph is borrowed. PMR scratch is charged before each
allocation; external analyses receive the remaining budget and their peak is
conservatively retained. The returned vectors are reserved before insertion.
The query is O((V+E+U) log V), including shared SCC and loop/dominance analyses;
its local indexes and propagation are linear. `VerifyForInPlan` re-derives the
raw certificate from the graph and compares every member. It does **not** claim
to be the independent tree/original-flow/recovery verifier still required in D/E.

Fixed-size `ForInFailure` carries real acquisition/op/value/use/block/edge IDs.
Kinds are Identity, Escape, AcquisitionOrder, Advance, Done, KeyBinding,
Activation, Exit, CatchState, ExceptionCorrespondence and Correspondence, plus
unchanged budget categories ResourceLimit/WorkLimit. Missing locations remain
missing. Publication/manifest plumbing is deferred until the route consumes
this proof, so the published first-refusal chains are unchanged.

`for_in_plan_test` covers normal/phased imports, copy/block splitting, SCC
transport through loops, certificate and raw-IR mutations, done-domain misuse,
escape, acquisition-order effects, handled-next refusal, branching exception
forwarding, a returning/cyclic/observable handler, and 200/201/400/800 loops with
exact byte/work boundaries. `for_in_fixture_test` checks real es2abc instructions
and handler scopes. See the fixture README for provenance and the explicit
Node/runtime verification boundary.

Remaining D/E: native ForInStmt; per-iteration declaration activation and RHS
scope (including the captured-key fixture's environment allocation); body exit
mapping and loop-variable live-out projection; independent tree, original-flow
and recovery checks; verified printing; Node differential and its mutations.
No new language premise is introduced by B/C. None of the five target records
is claimed complete by this increment.

### B/C verification results

- Full keep-going build succeeded: `/private/tmp/r51bc-accept-build.log`.
- Fixed source regression: 65/65 (`/private/tmp/r51bc-fixed.log`).
- New tests plus the 24 loop/exception/iterator/source tests listed above:
  26/26 (`/private/tmp/r51bc-accept-targeted.log`). No pre-existing failing test
  was selected by this bounded regex or had its expectations changed.
- Both final cohorts against explore51/explore51s: 497/620 and 534/620,
  zero status changes and zero source-byte changes, including MockKit's earlier
  loops. Outputs: `/private/tmp/r51bc-accept-cohort/explore51bc{,s}`; log:
  `/private/tmp/r51bc-accept-cohort.log`.
- The six target raw loops all passed BuildForInPlan and VerifyForInPlan:
  m29450 90/101, m42548 23/34, m44469 28/39 and 179/190, m21553 47/58,
  m54047 230/241 (acquire/next PCs). Each acquisition handler has a forwarding
  certificate. Their record-level first refusals remain exactly the table
  above; DataDriver still stops earlier at the separate m42717 iterator.
- Measured work and minimum additional byte budget (input IR excluded):

| Loops | Work units | Byte boundary |
| --- | ---: | ---: |
| 200 | 967302 | 2867539 |
| 201 | 972137 | 2881869 |
| 400 | 1934302 | 5733539 |
| 800 | 3868302 | 11465539 |

Each exact boundary accepts and boundary minus one refuses with the proper
resource/work kind. These are query-budget tests, not a process-RSS benchmark
and not evidence that native per-iteration binding is complete.

### D/E work in progress: native statement syntax, not source admission

The b5009dc7 continuation added a typed `ForInStmt` with an owned
RHS, assignment target symbol and body. Structural checking and label planning
recognize it as a loop. Its source-side recovery program evaluates the RHS
once, acquires once, advances on normal/continue completion, and defines the
key only on the non-exhausted branch. Break and labelled outer continue use
the existing target identities. Acquisition, advance and key assignment have
distinct ordered source actions and conservative exception exits. Existing
key declarations are retained; no binding motion or loop layout rule changes.

This is **not completion of D/E**. EvaluationPlan and FunctionTree do not yet
consume ForInPlan. No enumerator operation has gained source admission. The
`kPerIterationLet` syntax alternative is deliberately rejected until activation
and RHS scope have independent correspondence evidence. The new
`for_in_tree_test` and `for_in_tree_source_test` use manually constructed source
trees; their Node checks are syntax/recovery-adapter checks, not a differential
of ABC recovery. The real es2abc fixture still only exercises the B/C proof.

The remaining semantic adapter must distinguish three boundaries: acquire
success creates private protocol state; advance success still includes the
undefined exhaustion result; only the successful done edge supplies a key to
the source binding. An ordinary expression result correspondence cannot
identify all three with a source key read. TreeFlow, OriginalFlow and recovery
must retain the native statement actions, edge-qualified key commit and all
consumed original sites together, then recheck every exit and exception route.
Only that adapter can authorize removing the enumerator from source bindings
or consuming the acquisition-handler forwarding certificate.

The captured-key fixture also needs more than an allocation exemption in the
first-event summary. In m524, PC10 creates the RHS environment, PC14 acquires,
PC26 pops that environment, and PC27 advances. The iteration environment is
created at PC39 after the done test and initialized at PC47. The acquisition
handler pops the RHS environment at PC21 before rethrowing. Activation proof
must cover both normal and exceptional scope exits and the distinct captured
iteration units. The current order check remains strict.

The new source-flow tests cover assignment-key zero iterations and last-key
live-out, break, continue, labelled outer continue, invalid target and invalid
per-iteration admission, plus exact byte/work budget boundaries:

| Statements | Work units | Byte boundary |
| --- | ---: | ---: |
| 200 | 46424 | 2459190 |
| 201 | 46655 | 2459871 |
| 400 | 92624 | 4917822 |
| 800 | 185024 | 9835086 |

These measurements cover the source-side flow builder, not the outstanding
original-flow correspondence or activation proof. Proxy, mutation and Symbol
runtime gaps described above remain unchanged.

Validation of b5009dc7 (not D/E completion): full keep-going build passed; fixed source
regression 65/65; for-in tree/Node/plan/real-fixture tests 4/4; the bounded
loop/exception/iterator/source slice 26/27, with only the documented pre-existing
`tree_structure_test` abort. No assertion in that test was changed. Final
exploration comparison against explore51b/explore51bs remains 497/620 and
534/620, with zero status changes and zero source-byte changes. Logs are
`/private/tmp/r51de-{build-final,fixed,new,targeted,accept-cohort}.log`.

All six target raw protocols still pass B/C verification. Both flags retain
the same published first refusals: assertContain m29450@90, Filter m44469@28,
core m21553@47 and MockKit m54047@230 are EvaluationError::kUnsupportedOperation
with EvaluationObligation::kPlan. Filter's second raw loop at 179/190 is proved
but source construction stops at the first loop. DataDriver still first fails
at m42717@153, PhasedMethodError::kInput > SemanticError::kUnsupportedInstruction;
its m42548 23/34 for-in protocol is independently proved. No target is newly
complete, and the original for-in source refusal has not been removed.

### Non-captured continuation: event evidence only (uncommitted)

`ForInEvents` separates the acquisition and advance Before/Commit identities
from the edge-qualified key write. An advance commits its raw next-result on
normal completion, including the undefined exhaustion value. Only the proven
not-done edge supplies that result to a source key binding; the exhausted edge
has no key write. There is no shared `ExprId` for these boundaries.

`BuildForInEvents` revalidates the B/C protocol and records operation, phase,
result and exceptional-exit identities. `VerifyForInEvents` independently
reads those identities and the decision's original successors, after checking
the underlying protocol. Its dense operation indexes use quota-backed storage
before allocation. The protocol pass finishes and releases its temporary
storage before the event indexes are allocated. Complexity remains
O((V+E+U) log V); the additional event pass is linear. Inputs and the supplied
plan remain caller-owned and are not credited as newly allocated storage.

This is **still not production D/E admission**. A trial evaluation/forest
adapter was withdrawn because the joint native-statement correspondence was
not implemented. In particular, the acquisition-only try ends before the raw
loop, whereas the native statement owns both acquisition and loop execution.
The pending TreeFlow/OriginalFlow adapter must consume the forwarding proof,
retain the acquisition and advance exception exits, bind the success-only key
write and verify all body/exit edges. Treating the protocol as inert anchors,
or assigning the raw next result an ordinary source expression, does not
satisfy that obligation. No production planner consumes `ForInEvents` yet.

`for_in_events_test` checks forged Commit, result and exception identities,
reversed done edges, a key write placed on exhaustion, omitted/duplicated
events, missing original exception evidence, and missing forwarding evidence.
It exercises 200/201/400/800 loops and exact byte/work boundaries. The existing
real fixture also checks event evidence for its non-captured protocols; m524
retains `kAcquisitionOrder`. These are IR/protocol checks, not engine-exported
Node differential evidence. The latter and joint OriginalFlow/recovery
mutation tests remain outstanding. The previously documented ordinary-object
Node limitation and Ark runtime-verification gaps are unchanged.

Validation of this event-evidence WIP: the full keep-going build passed;
`ctest --preset source-regression -j 1` passed 65/65. Plan, event and real-fixture
checks passed 3/3. The 30-test loop/exception/iterator/source/recovery slice
passed 29/30; only the previously documented `tree_structure_test` aborted.
Both exploration flag sets were compared with explore51d/explore51ds: 497/620
and 534/620, zero status changes and zero source-byte changes. All six target
raw protocols passed event verification, but none gained source admission.
DataDriver still first refuses getiterator in m42717@153; its independently
checked for-in is m42548@23/34. The other targets retain their acquisition
EvaluationError::kUnsupportedOperation refusals. Logs and outputs are under
`/private/tmp/r51e-{build-full,fixed,events,targeted,target-protocols,accept-cohort}`
(with `.log` for logs). The targeted test-name list is
`/private/tmp/r51e-targeted-names.txt`.

## Round 52 increment I: production evaluation, publication still refused

The two phased production callers (`phased_method` and `family_tree`) now use
`BuildForInEvaluation` through the common evaluation constructor. Its entry is
semantic: a live `ValueEnumeration` must have a complete `ForInPlan` and
`ForInEvents` proof. No sample, instruction sequence, adjacency or graph-shape
condition is added. Normal-flow source admission remains unchanged: its raw
protocol proof is not yet a normal expression-order adapter.

The raw proof retains dense operation, value and use membership. Enumerator,
next-result, done-test and decision identities are separate. The acquisition
RHS and the real key observations remain ordinary evaluation uses. Protocol
copy/phi SCCs retain their canonical VM identity; this does not assign either
the enumerator or the next-result a source symbol. Original phi occurrences
remain in the copy table with `kForInProtocol` ownership and no executable
parallel-copy schedule. Phi verification reconstructs that set from the graph;
it does not trust a suppression flag. All other phi inputs keep their ordinary
copy and exception-staging checks.

`kForInEvent` is a protocol placement, not an effect-free anchor. Acquisition
and advance still contribute ordered, throwing events to the phased corridors;
this increment does not fuse expressions into them. The evaluation verifier
reconstructs identities, uses, loop edges and event membership before accepting
these placements. Independent ordinary uses and effects still go through the
existing planner/verifier. All new vectors are charged before allocation using
the existing quotas. Shared identity/dominance/loop indexes and a bounded number
of verification passes keep total complexity O((V+E+U) log V).

Tree entry re-verifies the evaluation plan and refuses *before expression-forest
or source-symbol construction*, with `FunctionTreeError::kSemantics` and the
new appended `TreeObligation::kForInCorrespondence`. The location is the first
acquisition's real method/op/block and original/working PC. This is the explicit
"native-statement correspondence not implemented" boundary, not a partial tree
or a publication certificate. `ForInFailure` also survives evaluation failures
as a fixed-size typed cause/location, including both JSON publication paths.
Existing error enum values and resource classifications are unchanged.

Tests include real fixture and target-method evaluation; escaped enumerators,
duplicated next, altered done edges, removed uses, forged member/value/use
indexes, protocol-event placement and Commit mutations; copy/block splits and
a backedge copy SCC; forged/omitted protocol phi ownership; 200/201/400/800
scaling and exact byte/work boundaries; diagnostic round trips (including PC 0)
and CLI/manifest agreement. The captured-key fixture still refuses acquisition
order. There is still no engine-exported native-for-in Node differential:
increments II (tree/TreeFlow) and III (OriginalFlow/recovery/publication) remain.

Final increment-I validation: the complete keep-going build passed; the fixed
set passed 65/65 after the build completed. The 37-test targeted slice passed
36/37, with only the previously recorded `tree_structure_test` failure. Both
exploration flag sets against explore51e/explore51es remain 497/620 and 534/620,
with zero status changes and zero source-byte changes. Only four target record
diagnostics change to the new tree obligation. All six target loops pass the
method-level evaluation check: acquisitions at m29450@90, m42548@23,
m44469@28 and @179, m21553@47, m54047@230. DataDriver's first record failure
remains the separate getiterator in m42717@153. Filter's two loops share the
single method-entry tree refusal at its first acquisition, PC28; there is no
fabricated second public refusal. CLI tests compare the actual record receipts
with `abc_semantic --record-source`. Logs use the `/private/tmp/r52-` prefix,
listed in status.md. No native-for-in source completion is claimed.

## Round 52 increment II: native draft and TreeFlow (unpublished)

`ForInEmissionView` interprets the verified exceptional control tree without
rewriting its nodes, arcs, loop discovery, decisions or follows. It retains the
original LoopId for each native enumeration and each break/continue. A consumed
protocol decision enters its proved successful arm (or its existing continuation
when that arm is empty); it never enters the ordinary `EmitBranch` path. Other
body operations and loops retain their original control and operation identities.
The builder and TreeFlow each derive this view from immutable inputs and the
independently verified ForInPlan/ForInEvents. Supplied consumed flags are not
certificates. Split ControlDecision prefixes are still a typed correspondence
limitation of this adapter, not a restriction of for-in semantics.

The draft contains assignment-form `ForInStmt`, an invocation-private key
binding, and the original body stores/declarations. The key binding is distinct
from the raw next-result and the internal enumerator; it is written only on
success, never with the exhausted Undefined result. Original lexical stores
stay in the body, so a shared outer key cell retains its initial value on zero
iterations and its last assigned value afterwards. No declaration becomes a
per-iteration declaration. Captured-key activation remains refused.

The native statement retains acquisition's original origin and scope; its RHS
is evaluated once. Before appending the statement, the original preheader phi
initializations execute in their original sequence. These are private transport:
ForInPlan's acquisition-to-first-next proof excludes intervening observable or
throwing operations, and construction does not coalesce their storage with an
invocation parameter or captured cell. Thus these private writes cannot change
what the RHS observes or expose a change when acquisition throws. The verifier
requires acquisition and native loop entry to have the same actual source
scope. No user statement/declaration is moved by a follow-selection heuristic.

Exhaustion copies are emitted after the native statement only when the original
loop transfer census proves exhaustion is its sole normal exit. With another
break exit, nonempty exhaustion copies remain `ForInError::kExit`: they cannot
be executed unconditionally after a break. Existing explicit break/continue
copies and targets otherwise stay in the ordinary loop machinery. TreeFlow
rechecks both done edges, all remaining transfers, source sequences and copy
ownership against the unmodified control plan.

Exception correspondence is typed per original entry: `PhysicalCatch` preserves
the original exceptional exit; `ForwardedExceptionRoute` records that exit,
handler entry, payload and final destination. The universal forwarding summary
must cover the entire consumed path. The view separately censuses every
incoming exception edge before omitting any handler path; a mixed physical and
forwarded shared entry remains `kCatchState`, not permission to erase its other
entries. Nonempty catch staging also remains `kCatchState`. Handled advance is
still refused by the raw protocol proof. Unrelated catches are unchanged.

TreeFlow retains `TreeForInEvents`: the original acquisition/advance Commits,
success-only key event, native statement, RHS, key, scope and exception route.
Acquire/advance are not represented by one shared source expression. This draft
is deliberately not a ConstructedFunction. `BuildForInTreeDraft` exposes the
same production draft builder to internal validation; the normal production
entry refuses after successful TreeFlow with the appended
`TreeObligation::kForInRecovery`, at the first acquisition's real original and
working PC. OriginalFlow/recovery joint verification and publication remain
increment III; no engine-output Node differential is claimed here.

New tests exercise that production draft builder, including the real es2abc
scan/exits/nested fixture; copy and block variations; a shared outer cell's
initial/last key; handled advance sharing an acquisition handler; captured-key
refusal; forged view bounds/forwarding payload/exit/path; deleted next placement,
wrong key or continue target, RHS/key scope and per-iteration binding mutations.
The 200/201/400/800 cases binary-search the actual minimum grant for bytes and
work, check the boundary succeeds and boundary-minus-one fails with the correct
resource class. A returned retained-byte count is not assumed to be the peak
scratch grant required by nested verifiers. All added indexes are precharged;
passes remain O((V+E+U) log V) with O(V+E+U) additional storage.

Increment-II validation: the full keep-going build passed, the fixed set passed
65/65, and the 39-test targeted slice passed 38/39 (only the registered
pre-existing `tree_structure_test` failure). All nine for-in tests passed.
The six actual target loops also pass the production draft builder and TreeFlow:
m29450@90/101, m42548@23/34, m44469@28/39 and @179/190, m21553@47/58,
m54047@230/241 (acquisition/advance). Both flag sets against explore52/explore52s
remain 497/620 and 534/620 with zero status or source-byte changes. Only the
four previously for-in-blocked record diagnostics move to kForInRecovery;
DataDriver still first refuses the unrelated getiterator in m42717@153.
Logs and exports use `/private/tmp/r52ii-`; see status.md for the exact paths.

For the 200/201/400/800 independent-loop fixtures, the minimum successful draft
byte grants were 9,277,041 / 9,323,363 / 18,541,441 / 37,070,241; minimum work
grants were 11,190,049 / 11,247,301 / 22,393,670 / 44,822,463. These are measured
semantic-budget boundaries (not peak RSS), with the exact and minus-one checks
run separately for each dimension. No record/process cap was raised.

## Round 52 increment III: original/recovery event correspondence

`OriginalEnumeration` retains the original acquisition and advance Before,
Commit, exception exits, the distinct next-result, success/exhausted edges,
source statement, RHS and key binding. Protocol results never acquire an
`ExprId`; `FindOriginalResult` is not used to fabricate their source identity.
Normal loop/body/exit origins and the physical control graph remain unchanged.

`FlowEnumeration` names native event ports: RHS completion -> AcquireBefore ->
AcquireCommit -> NextBefore -> NextCommit -> DoneDecision. Exhaustion exits;
success executes KeyWrite -> KeyCommit -> body. Continue returns to NextBefore.
KeyCommit has a typed, success-edge-qualified definition separate from next's
Commit (which also commits undefined on exhaustion). Ordinary phi copies remain
ordinary definitions; a key contribution to a live-out phi is admitted only
when its real incoming edge is dominated by the successful iteration. No key
write or invented undefined assignment is placed on exhaustion.

`VerifyForInRecovery` runs after original mapping in **every** BuildRecoveryFlow,
including the conservative initial proof before FunctionTree can be published
and later recovery proposals. It rechecks raw event/result/phase/exit identities,
both done edges, native port links, key input identity and actual break/continue
jumps. Each current key input must be dominated by the success-edge KeyCommit;
hoisting it before acquisition or onto exhaustion invalidates the projection. It queries reset-aware declaration initialization at KeyWrite; being
non-captured never removes PutValue's possible throw. A key domain remains
conservatively Language rather than assuming a String on Ark's proxy path.

`FlowForwardedException` is distinct from a physical catch. Recovery repeats
same-payload, inert-prefix, final-destination and incoming-entry checks over the
original forwarding subgraph. Reverse Kahn termination rejects cycles and
non-throwing exits. Only then does the consumed Catch get typed forwarded phase
coverage. A shared handler's other entries cannot borrow this evidence. The
initial increment still refuses nonempty staging and handled advance. No other
catch is simplified.

No buffer is allocated from an uncharged protocol census. Retained tables use
TreeCheckBudget; temporary adjacency/worklists use RecoveryScratch. Recovery
uses O(V+E+U) additional storage and O((V+E+U) log V) time including the existing
initialization/dominance machinery. No record/process/work cap is increased.

`for_in_native_fixture_test` exercises the real production builder; the new
`for_in_source_test` exports native.abc under both contracts and runs Node
against its separate ordinary-object source oracle. It covers inherited and
shadowed keys, ordering, zero/last key, break/continue/return/throw, nested loops,
getters and callback throws. Key live-out across break is covered through a
property target. A local-key loop with break and nonempty exhaustion copies
remains `ForInError::kExit`, recorded by key_break.abc: emitting those copies
after an arbitrary break would overwrite the actual key. This is a remaining
representation gap, not a language restriction. Captured m524 and handled
advance retain their prior typed refusals.

Mutation checks cover raw Commit/exception/identity/done evidence, forwarding
payload, source event effects, writes on exhaustion and real continue edges.
They also detect source mutations losing inherited keys or zero-iteration key
values. The production-draft scale test covers 200/201/400/800 loops and exact
byte/work success and minus-one boundaries. Proxy/mutation/Symbol behavior
outside the ordinary-object cases remains the pinned-Ark verification gap
listed above; the Node run is not evidence closing that gap.


Increment-III measured production-draft byte grants (200/201/400/800 independent
loops) are 13,267,260 / 13,320,648 / 26,517,148 / 53,016,924; work grants are
10,424,631 / 10,477,632 / 20,874,641 / 41,809,540. Each exact grant succeeds;
one fewer byte/work unit fails in the corresponding resource class. These
measure admission, not process RSS.

Final cohort replay against explore52b/explore52bs yields 498/620 and 535/620
(+1 each), no status regression and byte-identical already-complete output.
Only four target first-refusal diagnostics change. All six target method loops
pass production draft + OriginalFlow + initial recovery verification. Record
results are identical under both flag sets:

| Record | Target method acquisition/advance | Published result / next refusal |
|---|---|---|
| o32/29268 assertContain | m29450 90/101 | complete; node --check passed |
| o32/42397 DataDriver | m42548 23/34 | unchanged SemanticError::kUnsupportedInstruction, m42717@153 |
| o32/43537 Filter, loop 1 | m44469 28/39 | TreeObligation::kDecisionEdgeCopies, m44496@252 |
| o32/43537 Filter, loop 2 | m44469 179/190 | same record-level next refusal |
| o32/21089 core | m21553 47/58 | ForInError::kCatchState, m21280@599, op215 |
| o32/53454 MockKit | m54047 230/241 | SyntaxResolutionError::kParameters, m54264; no published PC |

Both original/working PCs are equal at the reported next-refusal sites. No PC
is invented for the parameter-resolution failure. Replay and per-method logs:
`/private/tmp/r52iii-delivery-cohort.log`, `r52iii-record-results.log`,
`r52iii-target-loops-delivery.log`; exports: `/private/tmp/r52iii-delivery-cohort/`.


Delivery validation: full keep-going build passed; fixed regression 65/65;
41 targeted tests: 40 passed, only the registered pre-existing
`tree_structure_test` aborted (its assertions were not changed). All 11 for-in
tests passed. The exact selected tests and individual results are retained in
`/private/tmp/r52iii-targeted-list.txt` and `r52iii-targeted-delivery.log`;
other final logs are `r52iii-build-delivery.log`, `r52iii-fixed-delivery.log`
and `r52iii-node-check.log`. No holdout, full-suite or RSS benchmark was run.
