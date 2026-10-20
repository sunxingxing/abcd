# Switch case ownership and optional-expression joins

2026-10-20, baseline `ca4fc005` (HEAD and independently built wtbase).
No new source-language premise. This checkpoint implements the shared inert
switch-break route, **not native effectful switch fall-through**.

## Original-edge diagnosis

`ControlError(3)` is `kCrossEntry`; `ControlOwnershipError(0)` is `kDuplicate`.
The witnesses are lexical ownership conflicts, not duplicated source operations.
Fresh production reports and original graph probes are in
`build/review/switch-ca4fc005/`.

| Record / method | Original evidence | Required proof / current boundary |
|---|---|---|
| o18/57687 m58255 `handleMessage` | B23 PC297 claimed by `[21,26)` and `[23,26)`. Case-test B3 PC45 matches `closeServer` and enters B23; `sendMessage` body B21 PC261 also reaches it, directly or through B22. B23 contains real property reads and calls. | Genuine adjacent-case fall-through. B23 must belong only to the later case. An earlier case needs an explicit original-edge fall-through port, not a duplicated effect or an epsilon bypass. Still refuses. |
| o19/18254 m18564 callback | B10's optional skip reaches B20 PC545 → B15 PC562 (e27). B11's other skip reaches B13 → B21 → B19; the called path B12 PC542 → B14 PC547 → B15 also reaches B19 PC570. B15 has only the inert jump e23 → B19, no live value/phi definition or input. | Threaded/unthreaded epsilon join inside the optional expression. Verified forwarding ports split only control ownership; ordinary optional-region/TreeFlow/recovery proofs still own the read, nullish decision and call. Implemented. |
| o32/40109 m40670 | op183 PC476 is the branch after a `noteq` of two `.name` reads (PC444/456/468); its missing-value arm calls `DeepTypeUtils.isPromiseLike` at PC500 and its other arm retains the comparison value. | A selected value is embedded in a larger decision prefix. The current normal-path prefix consumer does not certify it as a `DecisionSelection`. `kPrefixArms` is unchanged; no branch is ignored or widened. |

The source oracle for o19 is
`samples/applications_app_samples/code/DocsSample/Media/Audio/AudioRoutingAndVolumeSample/entry/src/main/ets/pages/AudioOutputDeviceChange.ets:218`:

```js
switch (deviceChangeInfo.changeReason) {
  case audio.AudioStreamDeviceChangeReason.REASON_OLD_DEVICE_UNAVAILABLE:
    audioRenderer?.pause();
    break;
  // Three other reason cases have no body effects.
}
```

The emitted callback retains the saved discriminant and the ordinary optional
call, with one continuing join:

```js
if (($value3 = deviceChangeInfo.changeReason) ===
    audio.AudioStreamDeviceChangeReason.REASON_OLD_DEVICE_UNAVAILABLE) {
  audioRenderer?.pause();
} else if ($value3 === audio.AudioStreamDeviceChangeReason.REASON_NEW_DEVICE_AVAILABLE) {
} else if ($value3 === audio.AudioStreamDeviceChangeReason.REASON_OVERRODE) {
} else {
  $value3 !== audio.AudioStreamDeviceChangeReason.REASON_UNKNOWN;
}
```

Case-property lookups remain in their original order, including the last
comparison, even when the case body is empty. No callee or receiver is reread.

## Implemented epsilon correspondence

Only an unmet duplicate-ownership census can request the new view. A successful
old plan takes precisely its old path. The shared index is built once per
builder/verifier invocation, not once per candidate block.

A `ControlForward{block,incoming}` names an original incoming edge into a block
with an acyclic corridor of one-normal-successor vertices, no live result/input, no phi/catch definition, no
outgoing edge-qualified phi input, no language effect and no possible throw.
The block and every incoming/outgoing vertex have the same original natural-loop
activation; loop headers cannot be forwarded. Environment changes, debugger,
return/throw, lexical writes, allocations and observable reads cannot qualify.
Admission uses typed semantics/def-use, never instruction names, PCs or samples.

For all but one incoming edge, the adjacency view has a distinct epsilon port
leading to the first non-inert original successor. The memoized corridor closure
also covers independently split edges; it never follows an effect, phi transfer,
activation boundary or cycle as though it were epsilon. The original block, operation, source anchor
and outgoing edge stay single-owned. This is the trace equivalence
`u -> inert b -> t` versus `u -> epsilon port -> t`; it is not duplication of
an expression, statement, definition, exception or source declaration.

`VerifyForwardingControl` rebuilds effect/SSA/activation facts from immutable
inputs, checks every port and original edge, and supplies a borrowed projection
to the existing independent `VerifyControlRegions`. The latter still proves
unique ownership, all entries, loops, exact targets and termination. No flags
from discovery constitute evidence. TreeFlow verifies the optional expression
and records the epsilon port explicitly; correspondence origins never invent a
statement or ExprId for it. OriginalFlow retains the original graph, including
its epsilon jump. Recovery still compares every observable/definition/exception
path with that graph, initially and for each recovery projection.

All vectors are admitted before allocation through `ControlQuota` / the existing
loop-forest budget. Original vertices/arcs are an unchanged prefix; additional
ports and arcs are bounded by E. Two independent shared-index derivations plus
the existing region/loop proofs are `O((V+E) log V)` with `O(V+E)` extra retained
space. No failed verifier is retried; a non-inert witness keeps the exact old
refusal. A resource refusal never becomes a proof.

## Remaining native-switch increment

The source tree currently has no `SwitchStmt`, and the control plan has no
case-body or fall-through ports. Factoring B23 as an if-follow would either
execute it on a no-match path or duplicate its effects. Neither is admitted.
Native fall-through therefore remains unimplemented at this checkpoint.

The required integration is explicit: derive a discriminant identity and ordered
strict-equality case-test relation; partition case bodies at their real entries;
own the target only in the next case; preserve the earlier case's original edge
as a fall-through port; represent default position and break ports; then add
source `SwitchStmt` and corresponding TreeFlow/OriginalFlow/RecoveryFlow case
ports. Non-adjacent shared entries must stay refused. Case expressions with
observable effects must preserve test order and evaluate the discriminant once.

Pinned frontend evidence:
`references/openharmony/arkcompiler/ets_frontend/es2panda/compiler/core/switchBuilder.cpp:44-66`
evaluates/stores the discriminant once, emits strict case comparisons and jumps
to their case labels;
`es2panda/ir/statements/switchStatement.cpp:46-77` tests non-default clauses,
jumps to default/end, then emits bodies in source order. The latter is why a
fall-through edge denotes the immediately following source clause rather than
permission to share arbitrary if arms. These are implementation evidence; the
semantic obligation is ECMA-262 CaseBlockEvaluation / CaseClauseIsSelected
(`https://tc39.es/ecma262/#sec-runtime-semantics-caseblockevaluation`).

## Verification checkpoint

- `control_forwarding_test`: original-ID permutation, split corridors,
  owner/target/port mutations; effects, throws, definitions and phi-transfer
  mutations reject. 200/201/400/800 width/depth chains and exact prepaid byte/work
  boundaries include the incomplete old census and independent verifier.
- `switch_optional_cli_test`: actual es2abc fixture with a counted loop, multiple
  switch cases and optional calls. 800 Node scenarios per flag set: null,
  undefined, changing getters, property/call throws, receiver identity, zero/last
  iterations; four mutations must change observations.
- `switch_optional_record_cli_test`: 960 independent original/emitted callback
  scenarios for the actual newly complete record, including changing reason
  getters, enum-property getters, logging, devices, receiver and throwing pause.
  The original .ets is never passed to the engine. Its sole callback parameter
  type annotation is removed only in the Node oracle.
- Both cohort comparisons: **538/620 unchanged** previous flags and
  **592→593/620** source contract; zero regressions and zero prior-complete text
  changes. All failure/status manifests are unchanged except o19/18254's new
  completion. Its entire emitted file passes `node --check`.
- SDK default/O0 fixture comparisons are **not byte-identical**: native switch
  dispatch recompiles from the recovered if ladder to sequential `stricteq`
  guards instead of the source switch's `strictnoteq`/target table. Feedback
  slots, branches and O0 register/copy layout follow that change. The recovered
  counted increment omits a redundant `tonumeric` before `inc` (its counter is
  proven numeric). Module entry matches exactly; both function bodies differ.
  ABI, method count, import/export tables, call/receiver/order and ordinary
  optional-call semantics are preserved. There are no handlers in this fixture.
  The checker reports **0 exact / 1 different record per mode**, not a pass;
  no normalization or new allowance was added. The grouping-only token check
  also correctly fails for a switch-to-if source rewrite.
- Full build passed. Focused final tests **5/5**, fixed source regression
  **65/65**, requested sweep **194/200**. The six failures are all reproduced
  with wtbase: family_source_cli, tree_structure, try_tree, class_tree_source,
  try_tree_source and file_generator_return_loop_source. No new failure.
- Exact admission at 800 units: **9,466,343 bytes / 3,750,042 work units**;
  limit-minus-one refuses. Conservative final four-worker-plus-coordinator OS
  RSS is **169,738,240 bytes**, with a largest individual child of 41,844,736.
  No Ark runtime execution is claimed.
