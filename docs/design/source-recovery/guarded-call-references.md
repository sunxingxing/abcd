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
