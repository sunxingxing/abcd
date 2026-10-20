# Guarded call references (2026-10-20)

Baseline: `0d9a1707`. ABC-only probes, raw instruction reports and value/placement
IR are under `build/review/guarded-references/`; no paired source is an engine
input. The two target records share the outer Reference refusal, but **not the
same failed proof**.

## Diagnosis before implementation

m28799 (`byMonthDayForYear`, o5/28303) is phased:

```
op20 PC56 ValueLexicalLoad depth0/slot0 -> v70 (captured TimeUtils)
op21 PC58 ValueGuard("TimeUtils"), Use23=v70
op24 PC66 ValueNamedRead(getPreviousMonthLastDays), Use26/base=v70 -> v73
op33 PC86 ValueInvoke, Use35/callee=v73, Use36/receiver=v70 -> v82
op183 PC488 ValueNamedRead(includes), Use208/base=v589 -> v592 (saved member)
op192 PC516 selection -> phi v37; op210 PC566 selection -> phi v38
op199 PC534 ValueCopy, Use226=v37 -> v602 (inside the argument expression)
op227 PC610 ValuePropertyRead, Use257/base=v82, Use258/key=v624 -> v627
op228 PC613 ValueBinary, Use259=v620, Use260=v627 -> v628
op231 PC620 ValueInvoke, Use263/callee=v592, Use264/receiver=v589,
                Use265/argument=v628
```

Use257 observes **op33's retained, multiply-used call result**, not the guarded
read v70. `ComposeCheckedCalls` propagates a guard ancestor through the call
result and records kUse when its sole-use fusion fails. Moving/re-reading op20
at PC610 would be wrong. The existing regular composition also stops when the
throwing PC516 selection is owned by the nonthrowing op199 copy: its
`RegionThrower`/`fits_catch` rule does not transport a throwing subtree through a
nonthrowing intermediate. Retaining existing module read/guard atoms alone does
not fix it (scratch-only experiment). This needs a transitive throwing-event
summary to the final execution root, preserving every original handler/catch
snapshot. It is kept separate and unchanged in this slice.

m50922 (o5/49599) is **normal flow**, with no checked-call diagnostic leaf:

```
op94 PC254 ValueNamedRead(Text.fontWeight), Use90/base=v98 -> v101 (saved member)
op99 PC266 ValueNamedRead, Use94/base=v103 -> v106
op102 PC274 ValueBinary, Use96=v106, Use97=v108 -> v109
op103 PC277 ValueBranch, Use98=v109
  op104 PC279 ValueLexicalLoad depth1/slot7 -> v115; op105 PC281 ValueGuard, Use99=v115
  op107 PC287 ValueLexicalLoad depth1/slot12 -> v110; op108 PC289 ValueGuard, Use100=v110
phi v16: Use110=v115 on the first arm, Use111=v110 on the other
op111 PC297 ValueInvoke, Use103/callee=v101, Use104/receiver=v98,
                Use105/argument=v16
```

Both guards already have verified `kCellGuard`/CellEvaluation initialization
witnesses. `IndexEvaluationUses` absorbs those guard dependencies, but
`IndexPhiExpressionInputs` counts them as extra observations and its raw tail
clock still treats them as foreign throwing events. The reads remain kRoot /
kCrossRegionValue instead of kPhiInput; selection discovery cannot close the
arms, so the saved member read cannot remain inline. This is the smaller local
consistency proof to fix. The source binding/TDZ owner remains mandatory: no
initialized-cell fact, guard deletion or new language premise is invented.

The selected captured constants are FONT_WEIGHT_FIVE_HUNDRED (slot7) and
FONT_WEIGHT_FOUR_HUNDRED (slot12). These are lexical cells in the incoming
creator environment, not module-slot loads. TimeUtils' call, separately, uses
CommonData.SCHEDULE_ARRAY.includes and a retained array returned by
getPreviousMonthLastDays.

## Local proof and independent checking

The common phi index first computes its unchanged old result. Only a normal
method with freshly verified CellEvaluation and an **unmet member Reference**
can request the additional view. `ReferenceArguments` is shared with ordinary
selection discovery; it rebuilds original placements after logical fusion.
Original phi dependencies propagate that demand through a prepaid worklist.
For an otherwise rejected phi input, a single initialized/source-TDZ guard
witness must refer to the same canonical load/storage; duplicate guards or
multiple phi occurrences stay refused. This is the current certificate boundary,
not a language invariant authorizing rejection of all redundant checks. A fresh original value-flow dominance
proof requires the guard to execute before the exact successful incoming edge.
The guard's real exception exit must retain its origin/effects and go to the
caller; handlers/staging are not synthesized.

Only those guards are treated as owned read dependencies on the phi-use census
and inert-tail clock, matching the ordinary evaluation-use census. A shared finite predecessor-event proof also requires the load to be the last
raw event before its guard, so another candidate guard is never hidden merely
because its own phi transport fails. The existing
all-path tail proof still rejects any foreign read/write/call, environment
transition, other parallel copy or divergence between the read and the edge.
The load therefore stays on its original conditional arm, with the same value
and original edge. The guard remains kCellGuard, backed by the **unchanged**
initialization/capture proof; no raw operation/exception is erased, no cell is
upgraded to initialized and no new contract is added. The real family/enclosure
still decides whether native lexical TDZ is valid. The early-capture fixture
therefore remains a ScopeError under previous flags.

Builder and independent evaluation verifier rerun this proof from the original
graph and freshly checked cells/phi copies. Normal selection, call-reference,
TreeFlow, original-flow and recovery checks then run unchanged. The member is
read once before the predicate/selected argument; no receiver re-read or sibling
read merging is introduced. Failed evidence retains the ordinary materialized
plan and kNotInline; no verifier exception is caught and retried. Phased checked
calls, deferred joint predicate/keep-edge admission and printing are untouched.

Work is O((V+E) log V) at most: one argument/phi census, finite worklist, one
shared dominance proof and the old all-path tail analysis. There is no per-call
or per-cell whole-CFG scan. Arrays/queues/summaries are charged before allocation
with TreeCheckBudget; resource/work exhaustion remains typed.

Pinned runtime: [LDLEXVAR handler](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L6067)
loads the cell; [named Hole guard](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L4107)
throws only on Hole; [RuntimeThrowUndefinedIfHole](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/stubs/runtime_stubs-inl.h#L1727)
constructs ReferenceError. Existing SourceTdz/capture proofs own the native
binding spelling, activation and read semantics; error messages/stacks are not
an equivalence claim.

## Validation boundary

The new C++ test covers conditional incoming-cell reads, canonical copies,
block-split phi transport, foreign observations, duplicate guards, no-unmet-
Reference stability, wrong/missing consumption, guard placement and exception
mutations, and 200/201/400/800 exact byte/work boundaries. The
[real es2abc fixtures](../../../tests/integration/fixtures/guarded_reference_arms/README.md)
run through exported engine output and Node: 48 scenario comparisons, 15
observed mutations, plus the previous-contract early-capture refusal. Captured
callees keep existing ordinary phi materialization; they are not a new call
admission. Imported receiver reads are initialized; cyclic module evaluation
before the creator runs is not claimed as solved.

Acceptance results and the two-record table are recorded in status.md. m28799's
nonthrowing intermediate/throw-summary work remains separate.

A later m28799 increment must transport the **set of original throwing events**
through pure intermediates to the final execution root, rather than pretending
that a copy opcode itself may throw. It must also reuse already-inline verified
module read/guard atoms in regular composition. Each event keeps its dispatch,
payload, pre-throw state and catch-copy obligations; the retained array is never
reloaded/recomputed. Neither change is made here.

## Original-event summaries through pure transports (2026-10-21)

Baseline/wtbase **24b01ff0**. Fresh decoding confirms the earlier m28799
classification. It is **not optional chaining**. LDLEXVAR at PC56 loads incoming
`depth0/slot0`; the named Hole guard at PC58 throws ReferenceError to the caller.
The native capture/SourceTdz contract still owns that binding. No new premise.

```
op20 PC56 -> v70; op21 PC58 guards v70 (Use23)
op24 PC66 getPreviousMonthLastDays(base=v70) -> v73
op33 PC86 invoke(callee=v73, receiver=v70, ...) -> v82
op183 PC488 includes(base=v589) -> v592               // saved reference
op192 PC516 selection -> phi v37
op199 PC534 copy(v37, Use226) -> v602                // nonthrowing transport
op200 PC536 module read -> v603
op201 PC539 binary(v602, v603) -> v604
op210 PC566 selection -> phi v38
op227 PC610 get(base=v82 through copies, Use257, key=..., Use258) -> v627
op228 PC613 binary(..., v627) -> v628
op231 PC620 invoke(callee=v592 through copies, receiver=v589, arg=v628)
```

Use257 is the **base of the indexed read**, not a call callee/receiver and not
the TimeUtils binding. Its actual producer is the retained, multiply observed
PC86 call. Checked-call diagnostics carried that guard ancestor through the
retained result and reported kUse. Replaying that call/read at PC610 would be
incorrect. This change **does not absorb it**. The local failure was the later
argument expression: a selection carries original throwing events, but its
nonthrowing copy could neither supply a catch dispatch nor describe its final
selection-join boundary. The old composition consequently could not reach the
saved member read at PC488.

The old composition runs first. Only an unmet member Reference requests the
new pass. A prepaid, OpId-indexed throw summary carries either no throw or a
representative of the subtree's original, catch-equivalent throwing events.
Each child insertion independently checks the original dispatch and absence of
catch-copy staging. A pure parent retains that summary; it does not acquire a
fabricated exception exit. If the parent has its own throw, its dispatch must
match too. Root staging remains at the original root; moving such a root as a
child still refuses. A pure selection transport retains its **join** as its end
boundary, rather than pretending that its copy was an ordered event. The
single-entry/copy-free path, original selection arms/edges, unique consumption,
receiver identity and event order checks are unchanged.

Already-inline module read/guard atoms may participate with their original two
events; module-read admission is unchanged. Guarded capture atoms still require
the existing guarded-read proof and real capture binding. Allocation exclusions
and LiteralPlan proofs are unchanged. The repair ends at the unmet invocation;
it does not fuse that call into a later decision/store or admit newly discovered
selections. The unchanged selection verifier must reproduce the original
owners, prefixes and inputs, changing only the verified execution root. Failure
keeps the old materialized plan and its located refusal.

VerifyPhaseCompositions reconstructs the event summaries and boundaries from the
original graph and the proposed ownership tree with an explicit post-order
stack. TreeFlow and every recovery projection then verify the original individual
action regions; a representative is only a catch-equivalence summary, never a
replacement action/ExprId. Original guard, load, invocation and exception events
stay separate. Work is O((V+E) log V) at most: constant-many shared indexes/passes,
linear ownership traversals, and disjoint committed top-level selection regions.
All new summary/seed vectors and stacks are admitted before allocation.

**Result:** PC620 now verifies. The record remains incomplete at **PC642**,
call241 / property102, without a GuardedCallError leaf:

```
op102 PC262 saved member read -> v526, Use115/base=v525
op104 PC268 outer buffered object allocation -> v528
... inner selections and PC620 includes call ...
op237 PC631 own definition(base=v528 through copies, Use270, value=phi v39, Use271)
op241 PC642 invoke(callee=v526 through copies, Use275,
                   receiver=v524, Use276, argument=outer object, Use277)
```

That is a separate saved outer member / staged object-definition composition.
It is not admitted by relaxing an allocation exclusion or by duplicating the
retained array. tree_failure.mjs now pins this exact next Reference refusal.
The guard's pinned runtime citations above remain the semantic authority.

Tests extend guarded_call_test and phase_composition_test: pure transports,
copy/block splits, wrong receiver/success/guard membership, every original throw
remapped to a different handler, foreign observations, multiple uses, and exact
200/201/400/800 work/byte boundaries (including one-less typed refusals).
The real compiler fixture and detailed round-trip limits are in
[guarded_reference_transports/README.md](../../../tests/integration/fixtures/guarded_reference_transports/README.md).
`phorphan` now joins the existing call-reference differential/mutation corpus.

## o26/4515: guarded late lexical writes, separate remaining proof

The label “late module write” is not STMODULEVAR/export ordering in the current
refusal. Fresh source-contract manifest evidence is
`CaptureCellError::kInput > CaptureError::kScope > ScopeError::kInitialization`,
m4696 originalPC254/working251, capture ordinal3 depth0/slot2, requested Language.
The storage belongs to the module entry's **lexical environment**, `isPlaying`:

- m5540 allocates a named 30-slot environment at PC0; creates m7782
  `onAudioInterrupt` at PC60; first slot2 initializer commits at PC618.
- m7782 creates async descendant m4696 at PC49 inside its implicit async handler;
  the callback is passed to the external member invocation at PC58.
- m4696 loads slot2 at PC242, checks Hole at PC244, then writes slot2 at PC254.
  The RHS false is already materialized at PC239/240; the intervening operations
  are copies. The guard dispatches to H623, not a synthetic caller exit.
- The other writes to the same incoming storage are PC273 (read261/guard263)
  and PC611 (read599/guard601). These fresh decoded pairs are evidence to
  investigate, not an opcode-template admission.

STLEXVAR itself does not check TDZ (`interpreter-inl.cpp:6118-6133`); the guard
before each write is essential. It would be wrong to describe this particular
write as unguarded, or to assume an external callback is invoked after PC618.
The existing requirement lattice forwards the write as Language, so Rebase
correctly refuses to invent an initialized capture at creation time.

A premise-free next slice needs a **guarded-store correspondence**, not a
module-write reorder: derive per-write original environment/slot identity,
RHS evaluation, the same-activation Hole test dominating successful Commit,
no bypass/reset, and original catch pre-state; prove that native lexical
PutValue throws at that same point. Only when *every* descendant write has this
proof may requirement forwarding carry SourceAccess with that certificate;
an unguarded write must still dominate as Language. Keep the native declaration
at PC618, with the real name and TDZ scope, and independently verify the store's
recovery event/exception mapping (including H623). This crosses capture demand,
ScopeFacts and lexical-store recovery; it is separate from the bounded expression
composition implemented here. No production capture/module rule changed.
