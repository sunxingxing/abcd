# Shared abrupt tails and control ownership

2026-10-20. Baseline: `aa545dd4`, including the independently built wtbase.
No source-language premise is added. This round changes control factoring only;
no original operation, value, edge or exception destination is erased or copied.

## Diagnosis before the change

`ControlError(3)` is `kCrossEntry`; `ControlOwnershipError(0)` is `kDuplicate`.
It records two lexical traversals claiming the same original control vertex.
It does **not** prove duplicate bytecode or authorize deleting an operation.
The following are fresh `abc_semantic --record-source --language-source` results,
with decoded PCs and the original control graph checked independently. Bounds
are `[first, stop)`; `exit` is the virtual exit. Graph/probe evidence is under
`build/review/control-ownership/` (untracked).

| Record / method | Duplicate and competing regions | Actual constructs / source meaning | Proof group |
|---|---|---|---|
| o18/57687 / m58255 `handleMessage` | B23 PC297, regions `[21,26)` and `[23,26)`; B21 PC261 branches to B23 (e31) or B22, which also reaches B23; switch test B3 PC45 independently reaches B23 (e6) | `switch (message.type)`: `sendMessage` conditionally calls its helper and deliberately falls through into `closeServer`. B23 contains real property reads/calls, not an empty epilogue. | Native switch/fall-through region ownership; not changed. |
| o19/18254 / m18564 callback | B15 PC562, arc27 B20 PC545 → B15; regions `[20,19)` and `[12,19)`. B12 PC542 → B14 PC547 (pause call) → B15. | A switch after the device-reporting counted loop. The old-device case contains `audioRenderer?.pause()`; its skipped/called paths share the same break jump. The loop is present but is **not** one of the duplicate owners. B15 is a jump to B19 PC570. | Shared normal switch/optional join. Could use an independently proven transparent jump port; not changed. |
| o32/40109 / m40670 `isEqualNodeOrPromiseOrFunction` | B8 PC208, incoming e0 from B0 (also e2 from B1/e10 from B6); regions `[8,2)` and `[5,4)`. B4 PC160 returns the equality call; B5 PC189/B6 PC196 share the early-return B7 PC203. | Nested short-circuit tests, early returns, then shared continuing type tests. Choosing private return B4 as a decision follow lets the inner region walk into B8, which the outer guard also owns. | Shared abrupt-tail factoring (implemented). |
| o38/13920 / m14655 click callback | B30 PC928, arc35 B23 → B30; regions `[17,exit)` (PC470) and `[4,exit)` (PC81). Guard predicates B9/B10 share B11 PC257 (`popMessage(); return`); B20/B21 share B22 PC565 with the same early-exit purpose. | Switch dispatch with guarded READ/VERIFY and WRITE cases, each followed by ordinary case completion. The early return has two predecessors; the final return is the continuing join. | Shared abrupt-tail factoring (implemented). |

Thus there are two witnesses in the largest common proof group, and two separate
switch/join obligations. The latter are not folded into this fix.

## Algorithm and independent check

The existing region census remains first. A successful census returns the exact
old plan. Only a duplicate claim can request **one** follow refinement, before
any independent verification; there is no retry of a failed verifier.

On the already acyclic normal/projection view:

1. Compute real all-normal-path postdominators, entry dominators and ReachMeet
   once, using the existing topological/LCA indexes. Scoped entries participate
   as separate virtual-root successors.
2. A candidate abrupt suffix has one actual terminal completion boundary, all
   paths reach that boundary, and its entry dominates every reachable vertex
   (ReachMeet equals the entry). It may contain arbitrary effects and internal
   diamonds. Those effects are still emitted exactly once. Normal try exits and
   unknown completion ports do not qualify.
3. Multiple incoming edges are permitted when the entry's immediate dominator
   supplies a branch arm postdominated by that entry. This is an all-path proof,
   not a direct-edge/PC/operation-template requirement.
4. The disputed continuing vertex must lie **outside** a candidate suffix.
   Dominator LCA checks this without scanning a suffix per query. The continuing
   region itself cannot be reclassified as an early return.
5. Refine lexical follows around these abrupt suffixes, retaining all original
   arcs. A refined follow is not real postdominance: a regional decision prefix
   must still reach its predicate on every path. A private abrupt fallback is
   not allowed to become the follow of a continuing arm with outside entries;
   that guard stays ordinary if/else instead of being folded into a decision.
6. Re-run the bounded layout census once. Any remaining conflict retains the
   typed ownership/continuation refusal. `VerifyControlSkeleton` /
   `VerifyControlRegions` independently check every original arc, unique vertex
   ownership, termination, entry and loop scope. TreeFlow and recovery continue
   to verify execution/exception correspondence; none of their checks changed.

No shared block is duplicated, even when it contains only a jump/constant return.
In particular this does not grant general shared-arm-prefix ownership or solve
switch fall-through. Conditional-arm edge corridors not representable by the
existing decision model remain typed refusals; linear block splitting, arbitrary
internal terminal-region diamonds and control-ID permutation are tested.

Two DAG layout passes, each with logarithmic LCA queries, remain
`O((V+E) log V)`. Shared summaries, optional terminal identities and every existing
recomputed work array are charged through `ControlQuota::Size` before allocation;
work from the incomplete first census is charged too. Exhaustion cannot produce
a plan. All exceptional routing and source binding identities stay original.

## Results

| Record | Before | After under source contract |
|---|---|---|
| o18/57687 | duplicate B23 | Unchanged |
| o19/18254 | duplicate B15 | Unchanged |
| o32/40109 | duplicate B8 in m40670 | Ownership passes; `FunctionTreeError::kControl > TreeObligation::kDecisionPrefix > DecisionPrefixError::kPrefixArms`, op183 PC476, block/control15. This next obligation is not changed here. |
| o38/13920 | duplicate B30 in m14655 | Complete, in both flag sets |

Both 40-input comparisons against wtbase: **537→538/620** previous flags and
**591→592/620** source contract; no regressions and no text changes to previously
complete records. The only new completion is SendCommandComponent. The generic
export helper counts metadata-only planned paths too (1,007 manifest rows);
the established executable-record denominator remains 620.

Original source, evaluation only:
`samples/applications_app_samples/code/DocsSample/DriverDevelopmentKit/ScsiPeripheralDemo/entry/src/main/ets/component/SendCommandComponent.ets:135`.
Its `switch (this.index)` READ/VERIFY cases become the following native guards;
the original call and property-read order is retained:

```js
// Original lines 140–151: case READ: case VERIFY:
//   if (this.blockStartAddr == -1 || this.blockNum <= 0) {
//     this.popMessage(); return;
//   }
//   if (this.index == READ) ...readBlockData(...);
//   else if (this.index == VERIFY) ...verify(...);
//   break;
} else if ($value0 === CommandType.READ || $value0 === CommandType.VERIFY) {
  if ($local0.blockStartAddr == -1 || $local0.blockNum <= 0) {
    $local0.popMessage();
    return void 0;
  }
  if ($local0.index != CommandType.READ) {
    if ($local0.index != CommandType.VERIFY) {
    } else {
      RpcTool.getInstance().verify($local0.blockStartAddr, $local0.blockNum);
    }
  } else {
    RpcTool.getInstance().readBlockData($local0.blockStartAddr, $local0.blockNum);
  }
}
```

The WRITE, INQUIRY, REQUEST_SENSE and READ_CAPACITY cases retain their calls and
early returns in the same if ladder. This is source recovery, not a printer
cleanup; no changes to the printer or to a successful existing plan are made.

## Tests and SDK comparison

- `control_shared_terminal_test`: 200/201/400/800 internal diamonds, linear
  block splits, control-ID permutation, normal/projected views, mutations of
  owner/arm/follow/entry and normal-vs-abrupt ports. Original block/edge counts
  remain exact. Each byte/work minimum passes and minimum-minus-one refuses.
- `shared_terminal_cli_test`: real es2abc fixture, three functions including the
  same guarded dispatch **inside** a user try/catch; 1,440 Node observations per
  flag set. Getter/call throws, receiver, changing predicate results and ordering
  are compared; five source mutants must change the observations.
- `shared_terminal_record_cli_test`: extracts only the real recovered callback
  with Acorn and independently executes the original .ets callback (never an
  engine input). **4,176** scenarios per flag set, including changing index
  getters, all cases, invalid ranges/data and getter/call exceptions. The entire
  published record passes `node --check`; erasing early returns is detected.
- `tree_failure_cli_test` now asserts the new record's completion and exact
  agreement between `abc_semantic` and the manifest.
- Full build passed; fixed source regression **65/65**. Final requested sweep
  **156/162**, with six known baseline failures (see status entry).
- OS peak-RSS reports from repeat full cohort runs: largest child 40,321,024
  bytes. Sum of the four largest child peaks plus the Python coordinator peak
  is a conservative aggregate bound of **164,216,832 bytes**, below 1 GB.

Pinned compiler SHA-256:
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
Commands/hashes are also in the fixture README. `grouping_bytecode.py` compiled
both independent source and emitted fixture with default optimization and O0;
identifier-valued string export aliases were adapted identically on both sides.
The source's declarations were separated from their export clauses only to give
the checker the same export-alias spelling; no body was edited.

Per mode, `classify` and `func_main_0` match **exactly**. `dispatch` and
`protectedDispatch` differ: the recovered reversed if ladder uses `strictnoteq`
instead of `stricteq` and lays out mutually exclusive arms in reverse order;
branch destinations, feedback-slot numbering, jump labels and O0 register/copy
placement follow that layout. The protected method's handler still covers the
entire dispatched body and reads the caught exception in the same handler.
No exception route, getter, call, return value or receiver is removed. The whole-file literal/module metadata and method ABI are unchanged in this
fixture; the documented method-layout differences are not normalized away. Full diffs and exact
method comparisons are retained under `build/review/control-ownership/fixture-bytecode-final/`.

These are **not** whole-file bytecode-equality passes: the strict checker reports
one differing record at each optimization level, and its grouping-only token
check correctly rejects a source-control rewrite. No normalization/allowance was
added to hide it. Original-edge TreeFlow/recovery, the independent verifier and
the Node differentials supply the semantic correspondence for the two changed
method layouts. The SDK and Node do not execute these fixtures on Ark runtime.
