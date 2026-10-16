# Round 80: synchronous iterator inventory and proof boundaries

Evidence-only result on `ce65e686`. No production recovery rule is changed.
The cohort is `harmony-exploration-2026-10-12` in
`tests/corpus/source-cohorts.json`; all 40 input hashes were checked.
All **15,673 code-bearing methods** decoded successfully (zero decode refusals).
There are **49 synchronous acquisition sites, 33 methods, 12 module records**.
This inventory uses `LiftIteratorInstruction` and `IteratorMode::kSync`, not an
opcode name, source text, record name or previous first-refusal list.

## Method-level proof inventory

PCs are original unless a working PC is explicitly given. Each method is queried
once even when it contains multiple acquisition sites. Async methods first run
`ProveAsyncCompletion` with its await sites and original-source mapping; the
synchronous probe's initial unsupported-async failure is not counted as the
for-of blocker. These are standalone conditional input proofs, not proof of the
whole record's capture/family/creation obligations. A verified method is not a
claim that its record completes.

| Record | Method | Acquisition PCs | First protocol gap / verified extent |
|---|---|---|---|
| o5/28303 | m28799 | 677 | Identity / iteration / references / events / evaluation and independent evaluation verifier pass. |
| o20/6952 | m7171 | 93 | Identity / iteration / references / events / evaluation and independent evaluation verifier pass. |
| o32/15324 | m15918 | 29 | CloseCompletionError::kOwnership / kBodyDispatch, TryId3 @84 (return read @285). A user try inside the body dispatches to its own handler. |
| o32/15324 | m16108 | 117 | Identity / iteration / references / events / evaluation and independent evaluation verifier pass. |
| o32/15324 | m16135 | 113 | Identity / iteration / references / events / evaluation and independent evaluation verifier pass. |
| o32/15324 | m16162 | 110 | Identity / iteration / references / events / evaluation and independent evaluation verifier pass. |
| o32/23058 | m23218 | 29 | CloseCompletionError::kCompletion > CloseEntryError::kCorrespondence > CompletionRelationError::kInput; original 99 / working 96; raw graph lacks normalized async effect/exception edge. |
| o32/23058 | m23246 | 29 | CloseCompletionError::kCompletion > CloseEntryError::kCorrespondence > CompletionRelationError::kInput; original 99 / working 96; raw graph lacks normalized async effect/exception edge. |
| o32/23058 | m23306 | 29 | CloseCompletionError::kCompletion > CloseEntryError::kCorrespondence > CompletionRelationError::kInput; original 99 / working 96; raw graph lacks normalized async effect/exception edge. |
| o32/23058 | m23334 | 29 | CloseCompletionError::kCompletion > CloseEntryError::kCorrespondence > CompletionRelationError::kInput; original 99 / working 96; raw graph lacks normalized async effect/exception edge. |
| o32/23058 | m23394 | 29 | CloseCompletionError::kCompletion > CloseEntryError::kCorrespondence > CompletionRelationError::kInput; original 99 / working 96; raw graph lacks normalized async effect/exception edge. |
| o32/23058 | m23454 | 29 | CloseCompletionError::kCompletion > CloseEntryError::kCorrespondence > CompletionRelationError::kInput; original 99 / working 96; raw graph lacks normalized async effect/exception edge. |
| o32/23058 | m23563 | 26 | CloseCompletionError::kOwnership; ordinary TryId1, body 8 / handler 20; AnalyzeExceptionForwarding proves same-payload forwarding. |
| o32/23058 | m23590 | 26 | CloseCompletionError::kOwnership; ordinary TryId1, body 8 / handler 20; AnalyzeExceptionForwarding proves same-payload forwarding. |
| o32/38853 | m39073 | 101 | Identity / iteration / references / events / evaluation and independent evaluation verifier pass. |
| o32/38853 | m39263 | 145 | CloseCompletionError::kOwnership; ordinary TryId1, body 18 / handler 25; AnalyzeExceptionForwarding proves same-payload forwarding. |
| o32/40109 | m40502 | 231 | Identity / iteration / references / events / evaluation and independent evaluation verifier pass. |
| o32/40109 | m40530 | 305 | Identity / iteration / references / events / evaluation and independent evaluation verifier pass. |
| o32/42397 | m42717 | 153, 429 | CloseCompletionError::kCompletion > CloseEntryError::kContinuation @692; inner-to-outer current completion not derived. |
| o32/42397 | m42744 | 120, 396 | CloseCompletionError::kCompletion > CloseEntryError::kContinuation @715; inner-to-outer current completion not derived. |
| o32/42397 | m42771 | 113 | Identity / iteration / references / events / evaluation and independent evaluation verifier pass. |
| o32/42397 | m42798 | 83 | CloseCompletionError::kOwnership / kLiveOut @481 for Return close @424; handler close @499 also has kBodyDispatch @424 (TryId3). |
| o32/46008 | m47128 | 173, 654 | Second close @900: kOwnership / kBodyDispatch, TryId6 @717. First close @400 passes local ownership/priority; whole-method nested obligation remains missing. |
| o32/58223 | m60088 | 469 | CloseCompletionError::kCompletion > CloseEntryError::kCorrespondence > CompletionRelationError::kInput; original 575 / working 509; raw graph lacks normalized async effect/exception edge. |
| o32/58223 | m60524 | 222 | CloseCompletionError::kOwnership; ordinary TryId1, body 204 / handler 216; AnalyzeExceptionForwarding proves same-payload forwarding. |
| o32/58223 | m60551 | 146, 452, 501, 1307, 1359 | ExceptionInputError::kInternalValue, working PC2064, SSA70; original PC2193, phi merges Language/Hole/environment/internal/enumerator domains. |
| o32/58223 | m61057 | 36 | CloseCompletionError::kCompletion > CloseEntryError::kCorrespondence > CompletionRelationError::kInput; original 93 / working 90; raw graph lacks normalized async effect/exception edge. |
| o32/58223 | m61458 | 170, 529 | CloseCompletionError::kCompletion > CloseEntryError::kCorrespondence > CompletionRelationError::kInput; original 321 / working 316; raw graph lacks normalized async effect/exception edge. |
| o32/58223 | m61486 | 148, 455 | CloseCompletionError::kOwnership; ordinary TryId2, body 126 / handler 142; AnalyzeExceptionForwarding proves same-payload forwarding. |
| o32/58223 | m61706 | 136, 765 | CloseCompletionError::kOwnership; ordinary TryId2, body 118 / handler 130; AnalyzeExceptionForwarding proves same-payload forwarding. |
| o37/20175 | m20512 | 294 | ExceptionInputError::kInternalValue, working PC804, SSA312; value produced @800 retains internal possibilities. |
| o38/4845 | m5312 | 604, 1267, 1882, 2648, 3486, 4559, 5367 | ExceptionPlanError::kLayout > CatchRegionError::kCrossingScopes, TryId4 body777 / handler803; seven acquisitions include destructuring. |
| o38/25106 | m25655 | 317 | ExceptionInputError::kInternalValue, working PC846, SSA326; value produced @842 retains internal possibilities. |

| Evidence group | Methods |
|---|---:|
| Evaluation verified | 9 |
| Close ownership | 3 |
| Async raw/working correspondence | 9 |
| Mixed transparent handler | 6 |
| Nested pending completion | 2 |
| Input value domain | 3 |
| Exception region layout | 1 |

The six mixed-handler methods span three records (event, DeepTypeUtils, service).
All six unmatched handler entries have a verified same-payload forwarding
summary. The nine async correspondence methods span two records (event and
service). These groups overlap by record; they are not additive completion gains.

## What each record still needs

| Record | Current production first refusal | Work still required |
|---|---|---|
| o5/28303 TimeUtils | m28799: EvaluationError::kUnsupportedOperation > kPlan > LiteralError::kFlow > EvaluationInputCheck::kLiteralStagedAcyclic | For-of evaluation already verifies. VerifyStaged requires a whole-CFG topological order (literal_plan.cc:566-597). A cyclic CFG requires a private initialization/activation proof in place of that prerequisite; the current evidence has no instruction PC and does not authorize discarding the cycle. |
| o20/6952 ComponentFactory | Complete | Preserve output exactly. Its compiler round-trip has the distinct mixed-handler limitation described below. |
| o32/15324 hypium/index | m15697 ClassSelfOrderError::kObservation | Class self-order proof first; then m15918's body-handler dispatch and any later member obligations. m16108/16135/16162 already pass the for-of evaluation proofs. |
| o32/23058 event | m23306 PhasedMethodError::kControl > ExceptionControlError::kMultipleExits; Try0 body149/handler174, working125/150 | Async raw/normalized edge/effect correspondence plus mixed forwarding handlers in m23563/23590. |
| o32/38853 DeepTypeUtils | m39263 kControl > kMultipleExits; Try0 body290/handler315 | Mixed ordinary forwarding-handler/protocol topology and its recovery correspondence. m39073 already verifies. No promise of completion after the control gap alone. |
| o32/40109 assertDeepEquals | m40670 ControlError::kOwnership > ControlOwnershipError::kDuplicate; node8/arc0, bounds5->4 vs8->2 | Ordinary control ownership, separate from for-of (m40670 has no acquisition). m40502 and m40530 pass the for-of evaluation proof. |
| o32/42397 DataDriver | m42717 kControl > kMultipleExits; Try0 body633/handler658 | Nested current-completion derivation at692 (m42744 at715), then effectful close/tree/recovery nesting. m42798 has a live-out state and body-dispatch gap. |
| o32/46008 configService | m47128 kControl > kMultipleExits; Try0 body398/handler423 | Second close's body dispatch @717 and whole-method completion nesting; second loop's captured binding/activation remains separate. |
| o32/58223 service | m58418 FunctionEmitError::kKind > AsyncCompletionCheck=1 @18 | Async normalization first; later async correspondence, mixed handlers and m60551's internal-value phi. |
| o37/20175 RpcTool | m20373 CaptureCellError::kInput > CaptureError::kScope > ScopeError::kInitialization; occurrence0 depth0 slot0 @46/43 | Capture lifecycle proof first; m20512 also fails standalone input-domain proof at804. |
| o38/25106 RpcTool | m25320 same capture chain and occurrence @46/43 | Capture lifecycle proof first; m25655 also fails standalone input-domain proof at846. |
| o38/4845 driver | m5312 PhasedMethodError::kRegion > ExceptionPlanError::kLayout > CatchRegionError::kCrossingScopes; Try4 body777/handler803 | Shared/crossing handler-region representation and destructuring protocol. No ownership seal is attempted on an invalid region plan. |

## ComponentFactory output round-trip: why the seal refuses

The engine's emitted `toArray` recompiles as m425 with one acquisition at94.
The close summary's five obligations and its independent verifier pass. The
unsealed region is **ordinary TryId1, body61 / handler88**:

* Property reads at67 and75 and the call at83 dispatch to handler88.
* Handler88 saves the caught accumulator, reloads the same value at90, then
  throws that value at92 to the caller. Acquisition at94 lies after this wrapper.
* `AnalyzeExceptionForwarding` independently returns a forwarding summary for
  the handler. This is not an IteratorClose region and must not be claimed as
  close ownership. There is no observable handler effect or changed payload.

`protocol_region_ownership.cc:44-50` intentionally requires **all** active
regions to be protocol owned. Even if that seal restriction were removed,
`protocol_topology.cc:40-42` independently refuses every OrdinaryTry, and its
edge construction drops RaiseTransfer links for protocol projection. Thus the
round-trip rejection is a conservative mixed-region representation limitation
for a semantically valid wrapper, not evidence that the emitted assignment-form
for-of changes IteratorClose semantics. Removing only the seal check is unsafe.

A generic next increment can compose independently verified forwarding routes
with ProtocolOwned units, retaining each original dispatch source/payload and
final destination. Ordinary user catches must retain their wrapper. Topology,
control verification, TreeFlow and OriginalFlow/recovery must all consume that
correspondence; it is not a local candidate/seal fix. The six cohort methods
above expose the same boundary without depending on compiler layout.

## Why no local production fix was selected

The largest missing group is the nine async methods. The close analysis rebuilds
a raw graph without the normalized async operation/Resume extension and then
requires edge equality with the normalized ExceptionFlow. For example m23306
original99/working96 has **one raw successor, two ExceptionFlow successors**;
the raw node is unknown and lacks the edge to working handler107. The first
failure is CompletionRelationError::kInput during correspondence, before an
outcome relation can be accepted. `completion_relation.cc:133-178` and
`raw_event_graph.cc:75-86,110-112` own this check. A missing edge is not permission
to label the operation nonthrowing. The existing async normalization/original
certificate must be supplied to the raw graph and independently checked.

The next missing group needs mixed exception topology (above). Remaining groups
need nested current-completion transport, source-cell/binding lifetimes, or
input/region semantics; none is demonstrated to be a local for-of proof-input
consistency defect. The staged-literal and m40670 control failures are separate
post-protocol analyses, not a reason to widen iterator ownership. Per the
requested stop rule this round changes diagnostics/tests only. No new source
premise is assumed and no production gate is removed.

## Reproduction and validation

`close_completion_probe ABC --acquisitions` scans all code-bearing methods and
reports its method count, decode refusal count and acquisition count.
`close_completion_probe ABC METHOD --evaluation [--async]` runs close summary,
independent ownership/control verification and independent ForOfEvaluationPlan
verification. `--boundary [--async]` retains the per-entry missing obligations.
The probe prints typed numeric enums/IDs and real original/working locations;
no diagnostic text is used by production analysis. All query storage remains in
the existing budgeted analyses; the inventory decodes one method at a time.

Evidence is retained under `/private/tmp/round80`: acquisitions.json, all 33
method close/full logs and record-source receipts, cohort outputs, build and
CTest logs. The cohort metadata and inputs are never changed. The only modified
code is the standalone test probe plus two registered diagnostic tests.

Validation observed in this round:

* `cmake --build build/regression -j 6 -- -k`: passed.
* `ctest --preset source-regression -j 1`: 65/65.
* New inventory/evaluation diagnostic tests plus existing close probe tests: 5/5.
* Existing for-of source/draft/scale/variants tests: 7/7.
* For-of/for-in/control/recovery targeted set: 31/31.
* Close/completion/raw-event/for-of/for-in foundation sweep: 36/36, including
  the existing mutation, 200/201/400/800 scale and budget tests.
* `ctest -R 'cli|corpus|failure|_receipt|manifest' -j 1`: 75/76;
  only the registered pre-existing family_source_cli_test failure
  (`node escaped its owner or origin arena`, family_source_cli.cmake:77).
* Own cohorts: old flags 526/620, V4 573/620. All 7,411 generated files,
  including manifests, are byte-identical to the retained prior-increment
  outputs in `/private/tmp/for-of-iii/cohorts`. This is supporting evidence;
  cc's independently built `ce65e686` baseline remains the formal comparison.

No new semantic mutation or Node differential fixture was needed because no
production fix was admitted. Existing scale/budget/mutation and real-es2abc
Node differential tests ran unchanged. No commit, push or stash was performed.
