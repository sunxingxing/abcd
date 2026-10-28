# Projected exception snapshots for member-call composition (proposal)

2026-10-28. Design only: no call/reference admission is implemented in this
increment. The single-pass review fix preserves partial publication separately.

## Confirmed blocker

Service m60551 is `asyncRunSpecs`. Its ordinary user try ends at original2118;
its catch binds `e` at2121. Both the callee property read2132 and call2208 are
inside that catch. Their original dispatch is the enclosing iterator handler
2443 (try22), not the catch whose payload is `e`.

| Original PC | Operation / source role |
| --- | --- |
| 2121 | `sta v16`: catch payload |
| 2123–2137 | load `console`, save receiver v18, read `error` at2132, save callee v17 |
| 2139–2161 | `"" + TAG + "stack:"`; imported TAG load2144 and named TDZ guard2146; adds2150/2158 |
| 2163–2179 | save e in v20; strict null/undefined comparisons2168/2174; branches2171/2177 |
| 2181–2182 | nullish arm: undefined, jump2193 |
| 2184–2188 | present arm: read v20.stack at2188 |
| 2193–2204 | append selected value and `""`; retain argument in v19 |
| 2206–2208 | load saved callee, `callthis1withname error, v18, v19` |

The call is ``console.error(`${TAG}stack:${e?.stack}`)``. The working counterparts
of original2132/2188/2193/2208 are1436/1492/1497/1512. The exact typed chain is FunctionTreeError1,
ExpressionError4, CallPlanError4, CallReferenceError1 (`kNotInline`),
EvaluationKind1, TemporaryReason4 (`kCrossRegionValue`). The last name is a
placement reason, not a distinct CallReferenceError enumerator.

A diagnostic probe using only ABC-owned inputs confirms:

- property/call OpIds588/619 and optional stack getter612;
- phi48 has the two actual normal inputs769/770; both producer/consumer
  correspondences and its language domain are valid;
- each exceptional bundle at588/592/593/594/597/612/613/616/619 has one original
  copy, already classified `PhiCopyUse::kForOfProtocol`; it has zero scheduled
  steps and no CatchSlot staging destination;
- the original scheduling domain returns zero phased selections. Discovery with
  the raw staging barrier disabled sees branch603 / phi48. This is a diagnostic
  candidate only; it is not a verified plan or permission to emit source.

`FindPhasedPhiSelections` (`phased_selections.cc`) marks ThrowCopies as staged
from raw `PhiBundle.count`. `PhaseCorridors::HasCatchCopies` does the same;
`SameCatch` also compares raw counts before checking projected assignments.
These checks do not consistently distinguish original protocol evidence from
executable catch staging. The uniform retry in `BuildPhasedEvaluationPlan`
requires an ordinary staging slot; this case has none. The selection remains a
control/phi value, so composition cannot own the full argument subtree and
`BuildCallPlan` refuses the saved member Reference. This is an overly
conservative composition boundary, not permission to drop exception evidence.

## Proposed owner and certificate

1. The exception-copy owner builds a shared **projected snapshot index** from
   original PhiCopies/ExceptionCopies and verified for-of use dispositions.
   Per throw, retain operation, original exceptional edge, dispatch/protection
   identity, original bundle range and an interned effective snapshot signature.
   Every original copy has exactly one disposition: retained CatchSlot assignment
   or certified protocol consumption naming the original UseId, phi input and
   close owner/seal. Empty effective state is explicit. An empty schedule alone
   never proves consumption. No physical edge, SSA value or original copy changes.
2. Source evaluation owns a retry certificate for the unmet Reference: property,
   callee UseId, saved receiver, call/execution root, argument forest, projection
   index identity, ordered events, selection edges/phi inputs, and per-throw
   coverage. Shared phased selection/composition owners use the effective index
   only after validating the protocol coverage. Getter precedes arguments,
   receiver identity/single evaluation, selected-arm execution and coercion order
   remain the existing common obligations. A retained nonempty snapshot still
   needs the existing uniform StageGroup/StageCoverage proof; it is never erased.
3. Legacy-first: after the existing call-reference proof refuses with its actual
   unmet Reference evidence, retry only with a verified nontrivial exception-copy
   projection. Return every existing successful plan unchanged. Keep the legacy
   refusal and add the failed projection/composition cause if the proof fails.
   The partial-publication fallback remains in force.

Independent verification must cover: PhiCopies and ExceptionCopies (every raw
use/disposition and slot); for-of/close ownership and seal (consumed uses really
belong to native protocol effects, with close order/priority and zero extra
closes); evaluation/composition (fresh snapshots, event and selection equations);
call plan (callee and receiver); tree operands/origins (original throwing actions
and all copy provenance); tree flow; OriginalFlow/RecoveryFlow (actual native
protection context and protocol completion, including closed-handler re-entry).
The snapshot index is evidence, never an ignored-anchor list. Existing emission
uses a native MemberExpr/CallExpr; no `.call` rewrite or hand-written close.

Use typed projection failures for revision/owner, copy coverage, phi/payload,
handler/protection, placement and resource/work limits. Existing CallReferenceError
and TemporaryReason survive when the retry does not close. Keep resource/work
classification terminal and consistent with the established cause envelopes.

For N operations/values, E edges, U uses and K copy/coverage occurrences, target
O((N+E+U+K) log(N+K+1)) work and linear retained storage: build indexes once,
intern sorted effective snapshots, memoize routes, and reuse dominance/availability
indexes. Precharge the retained legacy plan, trial peak, all arrays, row storage,
sorting and verification work. No per-call graph traversal or whole-bundle
comparison at every expression ancestor.

## Minimal fixtures and risk

The following pinned O0 JS already reproduces the exact six-cause chain on the
current binary (m415, original/working155, property42, call66):

```js
export function run(log, fn, items) {
  for (const x of items) {
    try { fn(x); }
    catch (e) { log.error(`stack:${e?.stack}`); }
    if (x.stop) break;
  }
}
```

The corresponding async function with `await fn(x)` reproduces it at m416,
original188/working164 (property47/call71). Scratch JS/ABC/manifests are under
`build/single-pass/partial-fix/call-repros/`; inputs were compiled with the pinned
`--module --merge-abc --extension=js --opt-level=0` command form. Removing the
post-catch break yields an existing completion and is a byte-stability control.

A future `projected_call_snapshots` ISA fixture should include sync/async forms,
imported prefix reads/TDZ, live catch data, nested iterator/pattern closes, a
callee getter replacing its method, argument getter/coercion throws, null and
undefined payloads, callee throws, close getter/call/primitive-result failures,
and pending-throw priority. Compare logs, receiver/result identity and error
kinds in Node. Unit negatives: forged protocol disposition, removed coverage,
wrong handler/phi predecessor/payload, an ordinary retained copy mislabelled
consumed, and a stale projection revision. Include register/copy/block invariance
and200/201/geometric scaling. Do not weaken existing unrelated refusals.

No new source premise is required: use only the already approved iterator and
array-pattern acquisition, external-exception/no-Hole and resource-failure
premises. All actual exception and close semantics remain proved. Estimated
size:300–600 production lines plus400–700 test lines; medium semantic risk at
the raw/projection boundary. Centralizing the effective snapshot interpretation
is preferable to local `count == 0` exceptions in individual consumers.

Visible later operations are a second logging call2256, checked lexical
`AssertException` load2266/guard2268, instanceof2272, assignments2286/2300,
setStatus2324, setResult2345 and later awaits. A read-only scan of the existing evaluation plan finds only the588/619 pair
unmet; the later logging callee is already inline. No further failure has been
proven: this round does not bypass the call gate or implement its repair. Both inner
single-pass scope proofs already close; downstream ABI/capture/recovery may
still refuse and must be checked after this separate increment.
