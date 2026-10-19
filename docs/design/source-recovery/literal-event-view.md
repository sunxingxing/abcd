# Conditional literal composition: normal fallback arms

The normal-flow integration completes o24/63384 (m65897 PC64). The production
allocation exclusion in `phase_composition.cc` remains unchanged. This is not a
new phased event mask or a joint predicate/keep-edge admission.

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

## Tests and remaining boundaries

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
