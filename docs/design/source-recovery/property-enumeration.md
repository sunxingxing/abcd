# Property enumeration — Round 51 working boundary

The approved target is native `for (key in object)` with no new source-language
premise. **Committed step A implements raw semantic lifting/import; the current
B/C increment adds raw protocol and exception-forwarding evidence. Native tree
projection and printing (D/E) remain unimplemented.** No source printer admission
follows from these operations or certificates.

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
