# Conditional literal event composition

The normal-flow integration completes o24/63384 (m65897 PC64). The phased
inner-selection/outer-definition extension is documented below. The ordinary
allocation exclusion in `phase_composition.cc` remains unchanged; neither path
admits the deferred joint predicate/keep-edge obligation.

`LiteralEventView` references the original graph and the existing independently
verified `LiteralPlan`. It records allocations, definitions and nested operands,
unique consumer occurrences, result/definition Commit phases and original
exception-exit indices. A functional parent forest resolves ownership with an
explicit stack and memoized roots. Each operation is visited a bounded number of
times; storage is prepaid with `TreeCheckBudget`. Staged statement literals stay
outside this expression census. Array-initialization plans require a separate
extension; an empty array already owned as a literal operand retains its original
operation identity here.

`VerifyLiteralEventView` reconstructs the expected census without consulting the
supplied view, then compares every occurrence. **Both APIs require the ordinary
LiteralPlan verification first.** They do not prove a proposed LiteralPlan,
conditional arm execution, final phi identity, or relocation by themselves.
No caller may use this census as a composition mask/publication certificate.

## m65897 evidence before integration (2026-10-19)

The failing production graph is **normal flow (`phases.size() == 0`)**, not phased.
The scratch probe at `build/review/literal-integration/value-ir.txt` records:
- PC10 op5 `ValueGlobalRead(Image) -> v22`; PC18 op8 `ValueNamedRead(create, u7/base=v22) -> v25`.
- PC24 op10 `ValueLexicalLoad -> v27`; PC30 op13 `ValueNamedRead(videoThumbnail, u11/base=v27) -> v30`.
- PC36 op15 `ValueUnary(u13/input=v30) -> v32`; PC37 op16 `ValueBranch(u14/condition=v32)`.
- PC39 op17 `ValueObjectAllocation -> v88`; PC45 op19 `ValueArrayAllocation -> v90`.
- PC49 op21 `ValueOwnWrite(params, u17/base=v88, u18/stored=v90)`; LiteralPlan owns op17 + definition op21, nested array op19.
- Join phi `v16`: `u79=v88` on edge2 (fallback), `u80=v30` on edge3 (retained value).
- PC64 op27 `ValueInvoke(u23/callee=v25, u24/receiver=v22, u25/argument=v16) -> v36`.
- op17 is `kPhiInput`, consumer u79; op19 is `kNestedLiteral`, consumer u18; op21 is `kInitializer`.
- op8 is `kRoot`, no consumer, `kCrossRegionValue`; op13 is `kPredicateSnapshot`, consumer u13.
`VerifyCallPlan` calls `MemberReferenceInline` for **op8/v25**, requiring inline ownership by u23.
Its failure emits `kNotInline > kRoot > kCrossRegionValue`: it is the saved member read crossing the selection, not an escaped argument allocation.
The minimal fix therefore belongs to normal `BuildLogicalValues`/its interval verifier, not only phased composition.
It must prove the literal arm through the event view, then consume the selection atom with the saved reference and call in their original order.
Seed only an unmet member reference; preserve the predicate snapshot, arm phi identities and original exceptions. No member re-read or phase-allocation exclusion change.

## Conditional arm proof and composition

`LiteralArmIndex` builds one shared dominator/event/use index over the original
normal graph. An arm records its original entry, exit and phi UseId, allocation,
committed value and event count. The allocation has one external observation:
that phi input. The dominated arm region contains exactly the LiteralEventView
forest (plus control transfers); every forest event dominates the successful
exit. A bounded topological check excludes cycles/re-entry and proves every
normal path leaves on that original phi edge. Each throwing event retains its
original exit; a handler-bearing arm refuses with `kCatchState`. The existing
LiteralPlan proof owns private construction, operand/definition order and
successful commits. An empty nested array remains an original operand event.

`FindConditionalExpressions` consults the arm proof only for an otherwise
uncovered retained-value/literal selection in an **unmet member Reference**.
Already admitted selections keep their original path. The existing predicate
snapshot and both phi occurrences remain intact. `BuildLogicalValues` composes
the original saved member read, conditional event and invocation using its
normal reverse-construction and forward-order proof. `VerifyEvaluationPlan`
first independently verifies LiteralPlan, then reconstructs selection coverage
and the event intervals. Existing CallPlan, TreeFlow and original/recovery
verification still check receiver, single consumption, truth and phi mapping.
No allocation becomes an effect-free anchor and no sibling read is merged.

`LiteralArmIndex::Verify` builds fresh indexes from immutable inputs rather than
trusting a supplied/cached arm. Failure stays a typed coverage, escape, order,
catch-state or correspondence refusal; unproved arms keep the original
`kNotInline` boundary. Resource exhaustion remains a typed resource refusal.

All vectors are charged before allocation. One CSR forest index and dominator
preorder prefix census cost O(V+E); use resolution and existing SSA/dominance
analysis retain the existing O((V+E) log V) bound. Each allocation has one phi
observation and is queried once. Before a dominated-region walk, membership and
its prefix count must agree; successful ranges are disjoint, since a foreign
allocation would violate coverage. Failed queries are cached. No per-allocation
whole-CFG scan is introduced.

## Normal-flow tests and original boundary

- `literal_event_view_test`: original bytecode/SSA phased fixtures, nested objects,
  empty arrays, exception exits, copies, missing/wrong members and consumers,
  revision checks, 200/201/400/800 widths and exact byte/work exhaustion.
- `literal_arm_view_test`: normal retained-value/literal member-call graphs,
  independent evaluation/call verification, wrong arm/exit/value/phi, duplicated
  or reordered event evidence, missing/changed catches and consumption,
  200/201/400/800 widths and exact byte/work exhaustion.
- `literal_arm_source_test`: pinned es2abc fixture, engine export, node --check
  and 140 original-source/engine-output comparisons. Covers conditional literal
  arms, `Image.create(v ? v : {id:1, params:[]})`, `obj.f(x || {p:[]})`,
  getters, callee lookup failures, branch throws, receiver identity and a callee
  replaced while evaluating arguments. Compiler/options/hashes are in the
  fixture manifest; no source oracle enters the engine.

This slice reuses the existing expression LiteralPlan and selection discovery;
staged statement forests and nontrivial multi-block initializer discovery are
not newly admitted. A graph-level arm proof is not permission to bypass those
existing construction obligations. No new language premise is used.
o24/27331's inner-selection/outer-definition ownership chain is deferred;
o7/28433 is excluded. Phased literal selections still require their own
composition integration. Run evidence: `build/review/literal-integration/`.

## m27565 evidence before nested integration (2026-10-19)

The production graph is phased (`phases.size() == 1414`), not m65897's normal route.
PC305 op107 `ValueNamedRead(showToast, u120/base=v108) -> v109` is `kRoot/kCrossRegionValue`.
The receiver is the saved result of op104's `getPromptAction` call (v106, copied to v107/v108).
PC311 op109 allocates outer object v111 (copy v112); LiteralPlan stages its definition op127.
PC319 op112 tests saved v113; PC320 op113 branches to the two original arms.
PC322 op114 allocates v124; PC328 op116 allocates array v126; PC332 op118 defines its array field (u128/base=v125, u129/value=v126).
PC341 op121 allocates v115; PC347 op123 allocates array v117; PC351 op125 defines its array field (u133/base=v116, u134/value=v117).
Phi v29 has u753=v128 (copy of v124) on edge257 and u754=v119 (copy of v115) on edge271.
PC358 op127 defines outer `message`: u136/base=v112, u137/value=v29.
PC369 op131 invokes u141/callee=v122 (copy of v109), u142/receiver=v107, u143/argument=v121 (copy of v111).
`MemberReferenceInline` refuses **op107/v109**, whose sole call-callee occurrence is u141; it is not an arm allocation escaping.
Both arm objects have verified expression LiteralPlans (op114/def118 and op121/def125), but remain root snapshots: phased `IndexPhiLiteralEnds` currently returns no intervals.
The accepted LiteralArmIndex rejects phased inputs, and its previous integration seeds only a selection observed by the call, not an outer definition.
The extension must first certify phased literal phi arms, then retain selection -> outer definition -> private outer forest -> invocation ownership.
Every original Before/Commit/exception identity and the real receiver remain authoritative; no phase-composition allocation exclusion may be deleted.

## Nested phased literal integration (2026-10-19)

The preceding boundary describes the implementation at 3168de42. The new path
uses the same LiteralEventView for phased arms. `LiteralArmIndex` now proves
normal-edge entry/exit coverage and Before/Commit dominance on the original
exception graph, while retaining every exceptional exit. All throwing arm
events must retain their catch and need no catch copies. The old normal-route
handler restriction remains unchanged.

`IndexNestedLiteralInputs` builds one definition-owner/use census, seeded only
by an unmet member Reference. Each selected arm still needs its own expression
LiteralPlan. Its sole external observation is the original phi occurrence;
that phi is the stored value of a definition in the enclosing private object.
Existing phased selection and object-operand proofs then establish the joined
value, conditional execution, and the enclosing object's ordered definitions.
No staged arm, escaping inner object, or joint predicate/keep-edge admission is
added. Splitting an initializer which already lacks a native LiteralPlan stays
refused; equivalent copies/splits at the proved arm boundary are tested.

`LiteralReferenceView` reconstructs the complete outer forest and the original
effect chain: saved callee/receiver -> allocation -> selected initializer and
other definitions -> invocation. Its allocation/definition span is a separate
typed composition boundary; the ordinary allocation exclusion is retained.
Every ordered forest event keeps the call's catch correspondence. Before full
membership may replace ordinary phase equations, the verifier re-proves inline
operand consumer identity, single consumption, fuse semantics and contiguous
interval order. Pure inline dependencies belong to the forest too. Implicit
own-definition base uses are construction correspondences, not a second read
of the final literal value. Existing independent selection, LiteralPlan,
TreeFlow and recovery checks remain in force.

All new indexes and work stacks are charged before allocation. Functional
ownership roots are memoized; original effects and operand intervals are
scanned once, not once per construction. Dominance/SSA work retains the existing
O((V+E) log V) bound. The arm and enclosing-reference indexes each have exact
byte/work boundary tests at 200/201/400/800 ordered-definition widths. Mutations
cover wrong phi/arm identity, duplicate consumption, reordered outer definitions,
a forged endpoint/consumer, missing composition, wrong catch and an escaping
inner value. The real es2abc fixture adds the selected field between ordinary
getter definitions, with 120 engine-output/Node differential scenarios.

The target's PC369 obligation is discharged. The same method now stops at a
later independent call: m27565 op337 PC903, whose saved property op313 is still
`kNotInline > kRoot > kCrossRegionValue`. This later obligation is not admitted
by the new view and remains outside this slice. o7/28433 remains excluded.
