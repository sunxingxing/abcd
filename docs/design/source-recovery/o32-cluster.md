# o32 cluster: fresh bytecode census

Baseline `f497690f`; the prompt enumerates eleven records. Evidence is the
production V5 export of the cohort's input 32, raw decoded methods, the SDK
disassembly, and standalone close probes, under `build/review/o32-cluster/`.
Source files are evaluation oracles only. Missing PCs below are genuinely absent
from the first failure; they are not replaced with a nearby instruction.

| Record | Method / original PC | Exact first cause (enum names) | Bytecode construct / additional evidence |
|---|---|---|---|
| 15324 | 15697 / absent | ClassSelfOrderError::kObservation (2) | `Hypium` static initializer: internal store PC28, method creation PC40, call PC51, public module store PC62; the closed-fields-only initializer proof fails. |
| 21089 | 21280 / 599 | FunctionTreeError::kControl > EvaluationError::kUnsupportedOperation > EvaluationObligation::kPlan > ForInError::kCatchState (8) > TreeObligation::kSemantics | `init`: a for-in advance protected by a real handler; current for-in proof only forwards acquisition exceptions, not handled advance state. |
| 23058 | 23306 / 149 (working125) | PhasedMethodError::kControl > ExceptionControlError::kMultipleExits (4) | Async body, synchronous iterator; original await/rejection99 maps to working96. Close entries fail correspondence there, before ownership. |
| 38853 | 39263 / absent (located evaluation op60 PC145) | FunctionTreeError::kSemantics > EvaluationError::kOwnership > EvaluationObligation::kProtocolContext (20), kMissingInput > TreeObligation::kSemantics | `getAllKeys`: for-in over the object, then for-of over `Object.getOwnPropertySymbols(object)`. Both plans exist; the tree's for-in verifier omits the sealed for-of input. |
| 40109 | 40670 / absent (prefix op183 PC476) | FunctionTreeError::kControl > TreeObligation::kDecisionPrefix > DecisionPrefixError::kPrefixArms (8) | `isEqualNodeOrPromiseOrFunction`: property comparison PC468, Boolean test PC473, branchPC476; kept comparison value versus conditional `isFunction` calls feeds a larger Boolean decision. |
| 42397 | 42717 / 633 | PhasedMethodError::kControl > ExceptionControlError::kMultipleExits (4) | Nested iteration. Inner return close633/H658 feeds outer703/H728. Entry and priority proofs now pass; inner private-state live-out and outer dispatch ownership do not. |
| 46008 | 47128 / 398 | PhasedMethodError::kControl > ExceptionControlError::kMultipleExits (4) | Two acquisitions; first close complete, second900 has body-dispatch gapPC717 through a per-iteration environment cleanup. Captured binding requires a fresh source activation. |
| 51260 | 51746, creation51442 / 40 | CreationContextError::kKind (3) | `definemethod` creates the instance-field initializer; it is stored in lexical slot0 rather than immediately invoked. Constructor installation/field initialization is not a free function creation. |
| 52417 | 52655 / absent (snapshotPC6) | FamilyEnclosureError::kStructure (1); independently located TreeStructureError::kBinding | `stub`: fresh GETUNMAPPEDARGS object stored as `this.params`. Destination lacks the required host argument-snapshot factory. |
| 53454 | 53795 / absent (snapshotPC6) | FamilyEnclosureError::kStructure (1); independently located TreeStructureError::kBinding | Nested `f`: fresh GETUNMAPPEDARGS retained and passed to `getReturnInfo`/`apply`; same missing host factory. Native `arguments` is not an equivalent object. |
| 58223 | 58418 / 18 | FunctionEmitError::kKind > AsyncCompletionCheck::kPreludeEffects (1) | Fresh environment and parameter stores followed by a hoisted `done` function creation before ASYNCFUNCTIONENTER at25. Allocation and capture publication need a private async declaration-instantiation proof. |

## Yield/risk ranking

1. Mixed-enumeration verifier composition (38853, upper bound1): reuse both
   complete proofs, carry the immutable for-of context through every independent
   check. Small, no new semantic admission; later blockers still require a full
   record run. Retained Boolean prefix composition (40109, upper bound1) follows
   if its existing selection proof is complete. These are the cheapest plausible
   record completions, rather than treating all enumeration refusals as one fix.
2. Private initializer/prelude proofs (15324,58223, separate upper bounds1 each):
   medium/large, distinct synchronous class and async promise-entry events.
3. Close composition (3 records, upper bound3): large; async source mapping,
   nested current-completion/private state, and sequential captured activation
   are three distinct certificates. First close passing does not certify method.
4. Instance-field initializer ownership (51260, upper bound1): medium/large;
   creation kind is meaningful and cannot be changed to an ordinary function.
5. Handled for-in advance (21089, upper bound1): medium/large, needs catch-state
   event correspondence beyond acquisition forwarding.
6. Argument snapshots (52417,53454, upper bound2 with an exact host adapter):
   current output contract lacks a supplied factory. The existing snapshot ABI
   is faithful but native `arguments` differs in identity, current-slot behavior
   and descriptors. No erasure/new premise or fabricated host binding is added.

All candidates remain subject to independent tree/recovery verification. No
existing successful projection is selected merely to alter its source output.

## Implemented: compose existing enumeration and selection evidence

**38853 completes.** In `function_tree.cc`, the independent for-in evaluation
check now receives the same immutable `ForOfEvaluationInput` as the constructor.
That exposes a second issue: the ProtocolOwned topology already consumed an
acquisition's transparent forwarding wrapper. A missing wrapper is not an empty
ordinary try. `ForInEmissionView` borrows its route only when the topology says
`ForwardedTry`, a fresh `AnalyzeExceptionForwarding` agrees on the original
handler/payload/destination, and the original exception exit is indexed by that
region. A typed `ForwardingOwner` keeps the handler's operations owned once, by
the protocol projection. Ordinary for-in forwarding retains its original path.

TreeFlow recomputes that view. ForInRecovery rechecks the borrowed original
exit, Catch payload and destination; ProtocolForwardingRecovery then uses the
native for-in acquisition's real event port, rechecking the same dispatch. No
fabricated ExprId/action and no transparent forwarding of an effectful close.
An intermediate cohort run caught two ordinary for-in regressions: their
topology has no protocol-role table. The role requirement now applies only to
the missing-wrapper/borrowed path. The final comparison has neither regression.

**40109 completes, including its later `isEqualSet` blocker.**

- m40670: `strictnoteq` PC468 produces the retained value; `isfalse` PC473 and
  branch op183 PC476 start its conditional consumption. The false arm retains
  the comparison, while the other evaluates `isFunction` at500 before joining
  at508. This complete selection was a normal-path `kRoot`, rather than a
  phased `kRoot` or `kLogicalValue`. The decision prefix now consumes its
  existing phi assignment as a conditional statement event. TreeFlow checks
  `LogicalAssignment`, including arms, edge copies, commit and consumption;
  nothing is admitted from a printed predicate or a local bytecode template.
- m40530: an ordinary inner loop's break/continue was incorrectly routed
  through the enclosing iterator's close. RecoveryFlow now prepays a statement
  enclosure index, distinguishing unknown from known-no-iterator, and decides
  whether a transfer leaves the iterator body from its actual target StmtId.
  The original target edges and close ports are still independently checked.
  Retargeting the inner break to the for-of is rejected.
- The next blocker was op177 PC445, `ToNumeric` on a proven Number. The operand
  checker already verifies and erases this identity; for-of exception recovery
  still demanded a physical source action. It now retains a typed
  `OriginalNumberIdentity` bound to the exact original UseId and real expression.
  Recovery independently rechecks the input's Number domain and correspondence.
  An omitted dead input has no invented flow point. The catch-domain solver
  depends on both the expression and any actual held-input completion, so a
  changed recovery cannot reuse the old exception exclusion unconditionally.

Pinned runtime evidence: `ets_runtime/ecmascript/interpreter/interpreter-inl.cpp`
4860–4873 handles `TONUMERIC_IMM8`: Number/BigInt are copied directly, while other
values call the potentially throwing runtime stub. The slow conversion is in
`ecmascript/js_tagged_value.cpp` 1852–1867. This change deliberately certifies
**Number only**, exactly matching the existing `NumberEvaluation` proof; it
does not classify arbitrary conversion as nonthrowing.

The new indexes are prepaid PMR arrays, traversed once. Route lookup and transfer
enclosure lookup are O(1) per original entry/statement; the existing sparse flow
solver supplies propagation. Work stays O((V+E) log V) for admitted work, with
typed resource/work refusals. Existing V2 iteration acquisition and V4 external
exception premises retain their meaning; no new premise or source contract is
introduced. No printer code changes.

## End-to-end results and remaining work

Baseline and wtbase are both `f497690f`. Both 40-input cohorts were regenerated
using `eval_cohort.py` and compared using `cmp_cohort.py`:

| Flags | Complete before → after | Regressions | Previously complete text changes |
|---|---:|---:|---:|
| `--language-numeric --language-constructors` | 541 → 541 /620 | 0 | 0 |
| `--language-source` | 599 → 601 /620 | 0 | 0 |

Only `o32/38853` and `o32/40109` become complete. All other nine first refusals
in the census remain unchanged, rather than being moved to less precise gates.
In particular, no nested/async close or captured iteration activation is
claimed complete. The argument-snapshot destinations need an exact supplied
host factory; native `arguments` is not substituted, and no runtime difference
is silently accepted.

Representative source comparison (originals are evaluation-only; exact full
Acorn slices of all three changed methods are retained in
`build/review/o32-cluster/method-comparison.md`):

| Method | Original source | Engine output |
|---|---|---|
| `getAllKeys`, m39263 | `for (let key in obj) { if (this.has(obj, key)) keys.push(key); }` then `for (const sym of symbols) ...` | `let $key2; let $key5; const $value1 = []; for ($key2 in obj) { if (this.has(obj, $key2)) $value1.push($key2); continue; } for ($key5 of Object.getOwnPropertySymbols(obj)) ...` |
| `isEqualNodeOrPromiseOrFunction`, m40670 | `aCtor !== bCtor && isFunction(aCtor) && isFunction(bCtor) && a instanceof aCtor && b instanceof bCtor && !(aCtor instanceof aCtor && bCtor instanceof bCtor)` | A retained `$condition15`, with `!(($value5 = a.constructor) !== ($value6 = b.constructor) && ... )`, then conditional `!!($value5 instanceof $value5 && $value6 instanceof $value6)`. Each getter/call remains single and ordered. |
| `isEqualSet`, m40530 | Outer index loop over two array pairs, for-of over each base array, inner index loop stops when found; `equalSet = equalSet && found`. | The same three nested loops, assignment-form `$key20` binding, ordinary inner break at exhaustion, `++$phi6`, then `$phi2 = $phi2 && $phi5`. Inner break never closes the iterator. |

Original paths are the bytecode artifact's evaluation-only Hypium files under
`src/oh_modules/.ohpm/@ohos+hypium@1.0.19/.../assert/deepEquals/`.
Neither these paths nor their source text enter production recovery.

## Fixtures, mutations and compiler evidence

Four new real es2abc fixtures cover mixed enumeration, the retained Boolean
decision, ordinary transfers inside for-of, and captured-array comparison.
Node differential: **318 cases**; the updated existing mixed-enumeration CLI
test adds **20**. Values, errors, receiver identity, getter/callback order,
zero/last values and body-throw/close-throw priority are compared. Both newly
published cohort files pass `node --check`.

Mutations cover a missing/shared forwarding route, wrong payload/destination/
owner, double handler ownership, forged continuation, missing for-of context,
wrong retained arm/copy/commit, wrong transfer target, wrong conversion input/
owner/domain/expression, and changed held-input provenance. Existing for-of
port/callee/receiver/exception mutations run on the new fixtures. Unit, width,
copy/register/block-split and 200/201/400/800 scale tests check exact admitted
byte/work boundaries; exhaustion remains refusal.

The existing `grouping_bytecode.py` was run on all four original/emitted fixture
pairs at default optimization and O0. The first run was unverified because its
grouping-only alias adapter requires equal edit counts; original exports need
zero edits while emitted quoted aliases need one. Applying the **same** Acorn
identifier-export-alias adaptation independently to each side fixes that SDK
limitation. The first report and the adaptation counts are preserved.

The rerun compiles/disassembles **16/16** sides successfully, with Node syntax
**4/4**. Strict comparison is **0 equal /4 differing /0 unverified per mode** at
record level; within methods it is **4 equal /6 differing per mode**. These are
not bytecode-equality passes. Registers, branches, exception ranges, literals
and metadata were not normalized. Grouping-only token equality is inapplicable
and fails, as expected for actual source recovery. The full differences are:

| Fixture | Raw difference | Semantic explanation / correspondence |
|---|---|---|
| main, default | One branch targets the existing nonthrowing jump at the body try's end instead of that jump's destination. | Same source continuation; shared forwarding plus original edge/handler correspondence. O0 also has private binding initialization/copies and explicit continues. |
| decision | Private condition variables, truth branches and phi copies; register/IC slot changes. | Conditional statement-prefix representation of the same independently checked selection. Getter/call/instanceof order, single evaluation and exceptions agree in the Node oracle. |
| local_transfer | An inert `j === 0` comparison at a tail continue disappears; IC slots renumber. O0 also changes private copies/loop form. | Both outcomes reach the same ordinary-loop increment. The operands are proven Numbers, so the strict comparison cannot invoke user code. Original control correspondence and transfer-target mutation remain checked. |
| set_equal | Private lexical initialization, source names/guard-name strings, register/phi copies, scope literal offsets, loop form and inert Boolean staging. Callback guards use the recovered capture names. | Fresh arrays/capture identities and source TDZ remain verified; the extra initialized guards cannot throw. Number conversion identity is the new explicit conditional fact, not a missing effect. Callback/compare/close order is checked. Method census, module exports and handler counts are preserved. |

Compiler/disassembler identities, fixture hashes and generation commands are in
`tests/integration/fixtures/enumeration_composition/README.md`. Full raw reports,
per-method counts and all `.pa`/`.diff` files are under
`build/review/o32-cluster/roundtrip-compatible/` and `roundtrip-methods.json`.
This is compiler/Node evidence, not an Ark runtime execution claim.

## Final verification

- Full build succeeds; serial `ctest --preset source-regression -j 1`: **65/65**.
- New/extended composition, retained-prefix, Number identity, recovery mutation
  and mixed-context targets: **11/11**.
- `enumeration|number_identity|for_in|for_of|logical|selection|tree|flow|recovery|cli|receipt|manifest`
  sweep, `-j 6`: **208/216**. The eight failures are
  `family_source_cli`, `logical_regions`, `logical_regions_source`,
  `tree_structure`, `try_tree`, `class_tree_source`,
  `phased_arm_selection_source` and `try_tree_source`.
  All reproduce against OLD: six via baseline CTest, family/class by the same
  commands with baseline executables (those targets are unregistered there).
  The logical test's bare `else` spelling assertion is also stale after the
  prior readable pass: the exact GuardedCopy source from OLD and NEW agrees.
  No assertion was weakened to hide these failures.
- The existing iteration-context CLI fixture's refusal becomes completion;
  its replacement asserts live-out/zero-iteration values, effects, errors,
  receiver and `abc_semantic`/manifest agreement against an independent oracle.
  No other existing expectation changes.
- Reports are `build/review/o32-cluster/final-{build,source-regression,new-tests,sweep}.log`,
  `baseline-sweep-failures.log`, `baseline-family.log`, `baseline-class.log`, and
  `verified-{source,previous}-comparison.txt`. `git diff --check` passes.
- A final rebuild's cohorts were also run through an OS `getrusage` wrapper:
  all 40 inputs per flag set finish and reproduce the same comparisons. Maximum
  individual CLI peak RSS is **42,074,112 /42,680,320 bytes** (previous/source).
  Conservatively summing each cohort's four largest CLI high-water marks bounds
  the four decompilers at **148,668,416 /155,828,224 bytes**; the four wrapper
  processes add at most **69,091,328 /69,271,552 bytes**. These are OS peak
  counters, not allocator totals or a claim of synchronized sampling. See
  `cohort-rss.json` and `rss-{previous,source}-comparison.txt`.

No commit, push or stash. The pre-existing untracked archive is untouched.
