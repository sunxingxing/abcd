# Iterator-close copies at multiple abrupt exits

Baseline: `1ea8d4e6`; original inputs only. Paired source is an evaluation oracle,
never an engine input. This increment supports multiple abrupt-close copies of
one synchronous iterator activation. It does **not** complete the five corpus
targets: their remaining obligations are listed below.

## Diagnosis

`ExceptionControlError::kMultipleExits` is value **4**; value **3** is
`kCrossEntry` (`src/source/exception_control.h`). This is an ordinary-try control
refusal, not an assertion that JavaScript for-of supports only one exit.

| Record / method | Original regions / exits | Missing proof before this change; remaining boundary |
|---|---|---|
| o37/20175 m20512 query | acquisition PC294; body Try3 PC339/H696; break close Try0 PC640/H665 (return read642); throw close Try1 PC714/H739 (read716). Break entry622; done branch328 reaches757; surrounding user Try4 PC236/H759 | The body walk entered the break-close copy. The shared continuation757 on the way to target848 originally caused external-entry witness328 Commit ->757 Before; the new exhaustion-corridor cut discharges it. The effectful outer user catch remains unprojected. |
| o38/25106 m25655 query | body Try3 PC362/H738; break Try0 PC682/H707 (read684); throw Try1 PC756/H781 (read758); break entry664; user Try4 PC246/H801 | Same shared exhaustion-continuation799 boundary (now discharged) and enclosing ordinary user catch (still missing). These methods have a loop break and body/close-error Throw entries, not an await or return from the loop body. |
| o32/46008 m47128 configService | Two **sequential** acquisitions. First body Try4 PC218/H380, close Try0 PC398/H423. Second body Try6 PC699/H880, close Try2 PC898/H923 | First close verifies independently. Second loop creates an environment at699, stores binding709, guard717; exceptions first pass cleanup H874 before H880. Missing body-dispatch/cleanup forwarding at717 and captured per-iteration binding proof. The old blanket nested-candidate refusal also remains; two acquisitions alone are not evidence of true nesting. |
| o32/42397 m42717 DataDriver | Outer body Try7 PC198/H823; inner body Try4 PC474/H760. Inner return-close Try0 PC633/H658; outer return-close Try1 PC703/H728. Inner/outer throw copies Try2 PC778/H803 and Try5 PC841/H866 | Derivation stops at outer guard PC692 (`CloseEntryError::kContinuation`). Need a correlated inner-output -> outer-input completion bridge; topology must chain these ports rather than make the inner Return terminal. Never skip the outer close. |
| o32/23058 m23306 async | Original close Try0 PC149/H174 (working125/H150); normalized await correspondence witness original99/working96 | Raw graph sees one unknown successor; verified exception flow has normal Commit and rejection Catch successors. Needs the verified await adapter in completion propagation/boundary checking. No deletion of the rejection edge or conversion to a NOP. |
| o15/37098 m37881 | No synchronous GETITERATOR in273 decoded instructions. Ordinary try includes property read582/call598/return609, handler612 | Unrelated `kCrossEntry`(3). Do not admit this through iterator ownership. |

The acquisitions are RpcTool294/317, configService173/654, DataDriver153/429,
and async29. configService has normal exhaustion plus Throw/cleanup paths;
DataDriver returns at757 after inner and outer inline closes (other return886
is outside that nested return path). Async rejection at99 enters the body's
Throw completion route. These are different composition obligations.

The RpcTool inner return-protection regions have multiple normal exits: missing
return jumps directly to completion continuation; successful call enters saved
error/result selection (m20512:654 ->690 versus663 ->667;
m25655:696 ->732 versus705 ->709). Thus ordinary try control refuses them before emission.
The existing close priority proof already understands those exits; the missing
piece was **composition of several complete close copies with the body wrapper**.

## Semantics and provenance

ECMA-262 [ForIn/OfBodyEvaluation §14.7.5.7](https://tc39.es/ecma262/multipage/ecmascript-language-statements-and-declarations.html#sec-runtime-semantics-forin-div-ofbodyevaluation-lhs-stmt-iterator-lhskind-labelset)
keeps exhaustion and same-loop continue on the iteration path. Other escaping
completions close the iterator. [IteratorClose §7.4.11](https://tc39.es/ecma262/multipage/abstract-operations.html#sec-iteratorclose)
keeps the original Throw even if lookup/call fails; otherwise a close error wins,
a primitive return result produces TypeError, and missing return/object success
preserves the pending completion.

Pinned frontend evidence:
`references/openharmony/arkcompiler/ets_frontend/es2panda/compiler/base/iterators.cpp:80-152`
(`Iterator::Close`), `compiler/core/dynamicContext.cpp:126-153`
(`IteratorContext::~IteratorContext`, `AbortContext`), and
`ir/statements/forOfStatement.cpp` (iterator context). The compiler saves current
completion, sets closed state before return lookup, catches the close error with
a Hole sentinel, then performs completion-specific selection. Analysis proves
**that expanded value flow**, not the semantics of a substituted runtime call.

No new premise: publication still requires IterationAcquisition (V2+) and the
separate external-exception-no-Hole premise (V4+) to discharge non-Throw sentinel
priority. Previous contracts retain their existing refusals.

## Composition and independent checks

1. Derive guard/reference/outcome/priority evidence for every close copy first.
   Construct shared sorted protocol+original-edge and protocol+original-node
   indexes only from complete semantic copies. No PC/template admission.
2. Derive the actual exhaustion corridor with the existing memoized, strictly
   register/effect-free transport proof. Cut close members before shared
   continuation nodes; retain private close jumps as close members. Preserve
   both original exhausted edge and canonical target in the summary. Topology
   maps a native Break to that same ordinary exhaustion continuation, so block
   splits do not invent two loop exits. The independent verifier compares both.
3. Independently derive ordinary loop body from value success and original
   dominance. Stop its traversal at all certified abrupt-close entries of this
   activation, including arms outside the natural loop. Keep a typed split:
   ordinary body, original dispatch, protocol close members. A copy never owns
   the enclosing wrapper; the wrapper owner must verify its own split. Other
   user tries and external entries remain refused.
4. Follow shared guard/saved-state reads through certified close members to
   Commit kills. Every ordinary live-out observation still refuses. A break
   close failure can enter the body handler, but its already-set closed guard
   prevents a second return call; outcome priority must still verify.
5. Count advance/value events through ordinary abrupt arms to each close unit.
   Close entry requires exactly one successful value read. Completion exits do
   not re-enter the next-call propagation. Original LoopIds/targets survive.
6. For the new split only, use backwards ordinary-observation liveness followed
   by closed private phi closure. A protocol Hole/catch payload never receives
   a source binding. Its ordinary uses remain subject to the original Hole
   proof. Exceptional phi staging has an explicit `kForOfClose` projection;
   builder and verifier derive it from the immutable split, and all staging,
   operand and flow consumers respect that projection. Ordinary catch slots
   retain their original pre-throw/Commit behavior. Partial/mixed ownership is
   not permission to omit an ordinary observation.

The independent close verifier re-derives all sets with reverse traversal and
compares every entry, original edge, split member and outcome. Evaluation,
TreeFlow, OriginalFlow and every recovery projection still verify independently.
Existing successful dispositions use the original root/staging projection.
This matters for zero drift: an initial broad liveness change renamed an
otherwise unchanged ComponentFactory temporary; the final projection is scoped
to the newly verified inline-close split, not applied to old successful plans.

Shared PMR indexes/sets/queues and all result vectors are charged before
allocation. Each original arc/use is indexed once; lookups are logarithmic.
Propagation is sparse and globally work/byte bounded; exhaustion is a typed
resource refusal. New boundary classification uses prefix counts and one live
member extent census, never a per-region rescan of the close members. Existing
outcome propagation remains separately bounded by its prepaid sparse arena.

## Tests and remaining work

`tests/integration/fixtures/for_of_multiple/build.json` records compiler SHA,
commands, source/ABC hashes at O0/default. `multi` publishes break, Return,
Throw and meaningful same-loop continue. Node compares270 independent traces,
including getter/call close errors, primitive/null/missing return, zero
iterations, body throws and receiver identity. `liveout` retains the edge-copy
refusal; labelled/async/nested/user-catch cases remain explicit negative fixtures,
not counted as semantic passes.

`for_of_multiple_test`, `_scale_test`, `_variants_test` exercise the production
identity/iteration/events/evaluation proof and verifier mutations (wrong port,
receiver/callee, missing exit, disposition, phi and catch staging), exact byte/
work boundaries, padding200/201/400/800 and register/block-split equivalents.
Close summary tests now verify all three copies in the real m423 fixture under
V4, retain missing priority under V1-V3, and reject split/port/state mutations.

`tests/tools/for_of_multiple_roundtrip.py` uses `grouping_bytecode.py`'s pinned
compiler/disassembler and strict assembly reader. No register/branch/handler
normalization. Both outputs compile in both modes. Default scan differs only in
inverting the final continue predicate and one redundant jump; O0 additionally
initializes the noncaptured assignment binding outside the loop and relocates
its compiler temporaries. Calls, three return lookups, object checks and their
order remain present. Module entry is identical in both modes. O0 recompiled
output passes the complete standalone protocol proof; default recompile refuses
ExceptionPlan at PC112. This is reported as a remaining round-trip analysis gap,
not a successful seal. Full diffs and probe output stay under the review folder.

Further work is intentionally refused: ordinary try + protocol topology composition; cleanup-forwarded captured loop
bindings; nested CURRENT-completion chaining; verified async raw-event adapter;
and edge-qualified live-out copies on exhaustion versus break. None can be
replaced by an exit-count/region-role flag or an empty plan.

## Acceptance checkpoint

Full build and source-regression 65/65 pass; six new tests 6/6 and the five
focused close/body tests pass. Requested sweep 192/198 retains five supplied
pre-existing failures plus the generator return-loop failure reproduced on the
freshly built 1ea8d4e6 baseline at loop_closure_file_test.cc:1311. The former
iteration-liveout ownership test now verifies the complete iteration proof;
production still refuses its edge-copy placement. The stop/finish and exception
loop fixtures now independently seal/control-verify and assert the precise
`kForOfCorrespondence` gate (always-abrupt body has no physical backedge/LoopId).
They are not counted as published or as differential successes.

Both 40-input cohorts stay 537/620 and 589/620. All manifests and all source files,
including partial outputs, are byte-identical. No requested corpus target
publishes. Review artifacts, strict SDK differences and baseline reproduction
are under `build/review/for-of-multiple/`; see status.md for exact paths/counts.
