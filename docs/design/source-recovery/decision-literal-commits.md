Chosen slice: literal-definition placements inside a verified selection predicate (o2/10579).
Yield upper bound: one record; a scratch-only trial completes its real production tree.
Reason: TreeFlow handles ExprId/AnchorId operands here but omits an existing PropertyId disposition.
Risk: low; no builder, evaluation admission, source contract, declaration or printer change.
Boundary: preserve literal ownership/order/escape/catch checks and all parked tracks.

# Evidence at 80515095

ABC-only probe of m11217 (OneMoment's generated child), original PCs:

```
op20 PC48  ValueBranch Use19=v35                         // first condition
op35 PC87  ValueInvoke Use32=v51 Use33=v48 Use34=v59 ->v62
op42 PC107 ValueObjectAllocation ->v69                   // buffered object
op44 PC113 ValueArrayAllocation ->v71, kNestedLiteral
op46 PC117 ValueOwnWrite Use42/base=v69 Use43/value=v71  // params definition
op50 PC128 ValueInvoke Use47=v67 Use48=v64 Use49=v69 ->v76
op51 PC132 ValueBinary Use50=v62 Use51=v76 ->v77
op52 PC135 ValueUnary Use52=v77 ->v78
op53 PC136 ValueBranch Use53=v78
```

The selection's scalar condition list owns the second predicate's original
blocks. `TreeFlow::Conditional` visits those operations through
`DecisionOperation`. The latter reaches its final kPlacement throw for op46:
EvaluationKind::kInitializer, OperationPlacement::PropertyId. This is not a
missing literal/selection proof, a Root expression, or a joint keep-edge case.
IndexPlacements already records the unique property placement and its stored
value. Objects rechecks exact allocation membership, ordered definitions and
origins; OperandCheck rechecks key/value identities; the original/recovery
checks retain the intrinsic property event and exception correspondence.

A scratch-only verifier adapter recognizing that exact initializer/property
correspondence makes o2/10579 complete. Production changes will only recognize
this already checked disposition in the decision-operand walk. A property with
a different disposition, origin or retained initializer must still refuse.
There is no new early return for arbitrary own writes or root assignments.

# Other fresh probes

- o32/52417: m52655, TreeStructureError::kBinding at the destination check for
  ArgumentSnapshotExpr. No HostArgumentSnapshotBinding exists; this is a real
  capability requirement, not a lost enclosure ID. The parked m53795 has the
  same failure. No admission changed.
- Remaining nonparked class initialization/capture, method-buffer/home-object,
  async prelude, exception/control and phased creator groups still require
  larger analyses. No fresh small composition was established for a parked item.

# Proof and tests

The adaptation consumes the existing independent initializer placement, not a
builder flag. Verified evaluation still proves private construction, conditional
execution and event/catch order; Objects and OperandCheck still re-derive the
allocation/commit/key/value chain; recovery rechecks it after every projection.
It introduces no buffers, CFG scans or new admission. Existing linear TreeFlow
work accounts for the same operation visit.

The change is confined to `tree_flow.cc`: recognize PropertyId only with
kInitializer, the same independently retained initializer ID and the original
commit origin. DecisionOperation also carries an operation location scope for
its typed Require failures. The normal/phased prechecks and final literal,
operand, OriginalFlow and recovery checks remain unchanged. This follows the
shape-restrictions audit: typed existing event correspondence, not a new
producer/opcode/layout admission. There are no new production allocations.

## Acceptance (2026-10-20)

- Full build succeeds. `ctest --preset source-regression -j 1`: **65/65**.
  New unit + real fixture: **2/2**. Focused tree/decision/logical/literal/recovery/
  guarded-reference set: **13/13**. The full named sweep is **145/154**.
- All nine sweep failures reproduce with freshly rebuilt **80515095** binaries:
  family_source_cli_test (owner/origin arena), literal_construct_test (:143),
  mixed_literal_test (:141), phased_literal_test (:707),
  literal_closure_source_test, unguarded_literal_closure_source_test,
  derived_literal_receiver_source_test, unguarded_derived_literal_receiver_source_test
  (family/tree cause8, check412), and decision_snapshots_source_test (unchanged
  spelling mutation). No unrelated expectations were changed. Commands, logs
  and baseline commit are in `build/review/next-slice/baseline-failures.json`.
- Fresh 40-input OLD/NEW exports against wtbase **80515095**: **531 -> 532/620**
  previous flags and **583 -> 584/620** source. **Only o2/10579 changes**;
  m11217 passes and the record completes under both contracts. Every existing
  file except o2's manifest is byte-identical. Three files are newly published
  per mode: source and two position maps. Both new sources pass `node --check`.
  No parked record's status or diagnostic changed.
- The real es2abc fixture has **288 Node differential scenarios** across both
  flag sets and **12 observed mutations**. The unit test rechecks semantics and
  recovery, and rejects wrong/dropped/duplicate property placement, key/origin/
  order and execution-edge mutations and a forged evaluation disposition.
  Equivalent register copies and a condition-prefix block split pass. A split
  inside an unfinished literal is not newly admitted by this slice.
- Exact **VerifyTreeFlow** byte/work boundaries (one byte or unit below refuses
  with kResourceLimit/kWorkLimit) are:

  | Predicates | Bytes | Work |
  |---:|---:|---:|
  | 200 | 1,983,817 | 879,495 |
  | 201 | 1,993,697 | 884,187 |
  | 400 | 3,959,817 | 1,762,965 |
  | 800 | 7,911,817 | 3,538,277 |

- Fresh full source-cohort maximum reported CLI peak RSS: **39,436,288 bytes**.
  No limit or budget changed. No ISA semantic support/classification changed.
  Logs, red/green evidence, IR probes, cohort comparison and RSS census are
  untracked under `build/review/next-slice/`. No commit, push or stash.

