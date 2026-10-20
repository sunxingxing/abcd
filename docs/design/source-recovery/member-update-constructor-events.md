# Member-update References and constructor-arm events

Baseline: **2e33dc77** (2026-10-20). This round publishes the constructor-arm
case. The member-update case remains refused; no multi-use or catch check was
relaxed to make its shared base inline. No new language-source premise.

## Original-value evidence

**o36/36985, m37638**: the saved `setOrCreate` property is op123 originalPC342
(working316), result v122, transported through v123/v134. Invoke137 at
originalPC378 (working352) consumes callee Use136=v134, receiver Use137=v120,
first argument Use138=v125 and second argument Use139=v133 (alias v132).
The mutation is:

```
load127 originalPC353 / working327 depth0/slot0 -> v126
copy128 -> v127; copy129 -> v128
get130 originalPC359 / working333 base Use128=v128, pool1193 -> v129
copy131 -> v130; copy132 -> v131
increment133 originalPC367 / working341 Use131=v131 -> v132
set134 originalPC369 / working343 base Use132=v127, value Use133=v132
copy135 -> v133
invoke137 originalPC378 / working352 ... argument Use139=v133
```

It is a prefix increment. The original base load is observed by **both** Get
and Set. The write already has assignment-result correspondence and is inline
at the argument. Treating the base as a sole-use operand would replay a mutable
capture after a getter/conversion callback. The current refusal is therefore
retained: `kNotInline > kRoot > kCrossRegionValue`. This is phased SSA (the
read/Get/increment/write have distinct Before/Commit blocks), not a missing
same-block optimization.

Pinned interpreter evidence: `references/openharmony/arkcompiler/ets_runtime/
ecmascript/interpreter/interpreter-inl.cpp:6442` implements named Get; `:3037`
implements named Set; `:4979` implements INC with numeric fast paths and a
throwing slow conversion. `:4860` implements TONUMERIC (Number/BigInt unchanged,
otherwise a throwing conversion). Postfix must return **that converted old
value**, not the pre-conversion object. No `+ 1` approximation is valid for
BigInt. These facts do not establish the missing source Reference ports.

**o7/28433, m28692**: saved `DynamicLayout.create` is member9@PC23 -> v28;
Invoke58@PC166 uses callee Use57=v28, receiver Use58=v25, argument Use59=phi19.
The true arm returns Construct35@PC98 -> v97 (constructor Use33=v82, argument
Use34=v84). The false arm returns Construct55@PC158 -> v50 (Use53=v35,
Use54=v37). Object allocations20@52/40@112 are the constructor arguments.
Nested member invocations31@83/51@143 compute the fields; own definitions32@89
and52@149 commit those values. **phi19 merges constructor results, not object
allocations.** LiteralPlan already owns the fields and nested calls. The old
conditional coverage scan saw the literal definitions as foreign to the
constructor's eager forest, so it left the saved member as a cross-region root.

## Implemented constructor view

`constructor_event_view.{h,cc}` joins verified literal ownership to the existing
inline-expression forest. A prepaid functional-parent index resolves each
operation once, iteratively, to its constructor invocation. Literal allocation,
each definition, nested operand, verified module/cell guard, constructor result,
Before/Commit identity, original exceptional exit and handler remain distinct.
The input table retains original UseIds, roles and canonical operands. A census
of **all** actual observations rejects a private value escaping the forest or a
constructor result with multiple external consumers. A selected result's only
external observation must be its original phi input.

`VerifyConstructorEventView` independently rebuilds the view from the original
value graph and independently checked LiteralPlan/evaluation placements, then
compares membership, roles, inputs, result consumers, commits and exceptions.
The normal conditional builder consults the view **only after the existing
coverage check fails**, and only for an unmet saved-member Reference argument.
It checks complete arm coverage, not merely permission to ignore the offending
definition. Existing original-edge selection, value availability, effect-suffix,
callee/receiver order, TreeFlow and recovery verification remain mandatory.
The allocation exclusion in `phase_composition.cc` is unchanged. No printer or
source-tree mutation was needed: the existing `NewExpr` owns the invocation.

Complexity: **O(I+U+V)** additional work/storage per method (the existing control
and availability algorithms retain their own bounds). No recursive walks,
per-constructor CFG search, or selected-arm rescans of a shared subtree. Every
vector/stack is admitted before allocation. The view and its verifier are paid
for once per conditional-discovery pass. Exact view+verifier budgets:

| Width | Bytes | Work |
|---:|---:|---:|
| 200 | 2,323,674 | 285,857 |
| 201 | 2,335,290 | 287,286 |
| 400 | 4,646,874 | 571,657 |
| 800 | 9,293,274 | 1,143,257 |

Each exact boundary passes; one less byte or work unit refuses. Unit tests also
exercise copies/register permutations, early escape, wrong arm/owner, missing
or duplicated event, wrong result consumer, wrong commit, missing exception,
wrong handler and changed operand/role. They do not claim a new phased
constructor-arm placement adapter; unsupported placements keep their refusal.

## Published method and independent source comparison

Recovered callback in `ChangeLayoutWithConditionVariable.mjs`:

```js
(elmtId, isInitialRender) => {
  DynamicLayout.create($local0.isHorizontal
    ? new RowLayoutAlgorithm({space: LengthMetrics.vp(10)})
    : new ColumnLayoutAlgorithm({space: LengthMetrics.vp(10)}));
  DynamicLayout.width("100%");
  DynamicLayout.height(150);
  DynamicLayout.backgroundColor(15724527);
}
```

Evaluation-only source, `samples/applications_app_samples/code/DocsSample/
ArkUISample/DynamicLayout/entry/src/main/ets/pages/responsivelayout/
ChangeLayoutWithConditionVariable.ets:28`:

```ts
DynamicLayout(
  this.isHorizontal
    ? new RowLayoutAlgorithm({ space: LengthMetrics.vp(10) })
    : new ColumnLayoutAlgorithm({ space: LengthMetrics.vp(10) })
) { /* child components */ }
.width('100%').height(150).backgroundColor(0xEFEFEF)
```

The source was read only as an oracle after recovery, never fed to the engine.

## Fixture and compiler evidence

`tests/integration/fixtures/member_constructor_values/manifest.json` records
commands, source/ABC hashes and the pinned compiler SHA-256
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
Two real ABCs use default and `--opt-level=0`, `--module --merge-abc`, identical
record/source names. All seven methods publish in each fixture. The three
constructor methods cover literal/array arguments, late getter/member-call
fields and both conditional arms. Node compares receiver identity, saved callee
lookup/replacement, noncallable timing, key/getter ordering, branch throws and
constructor throws. The four ordinary parameter-based update methods were
already supported, retain their old lowering, and are **not** new member-update
Reference admission. Their existing values/ToNumeric/get/set/throw behavior is
also checked. The final differential has **4,688 positive cases**, plus **10
observed semantic mutations** (wrong arm/constructor/argument/receiver and double
constructor execution). Both fixture outputs and both new cohort records pass
`node --check`.

`switch_roundtrip.mjs` creates symmetric test-only per-method wrappers preserving
each source/generated body and signature. `grouping_bytecode.py` recompiles them
at default and O0 and compares instructions, registers, branches, handlers,
constants and metadata without normalization. The **three constructor methods
match exactly in all 12 comparisons** (3 methods × 2 input modes × 2 recompile
modes). Its grouping-only token check correctly rejects parameter renaming;
that is not claimed as a grouping pass.

The unchanged update lowerings have **6 matching / 10 different** comparisons
across the same two input modes. These are not passes or normalized away:
prefix/saved-member O0 use explicit scratch initialization and numeric saves;
compound O0 saves the key/result and later rereads stable parameter bindings;
postfix uses a private ToNumeric scratch and an additional increment of that
private primitive, discarded before the actual retained old value is returned.
That extra private calculation remains visible in optimized bytecode too. Node
checks the ordinary semantic domains, but these comparisons do not establish
the requested native-update Reference correspondence. Raw reports/diffs are
under `build/review/member-updates/bytecode-{O0,default}/`.

## Remaining member-update implementation boundary

The tighter implementation is one native member-mutation action, not a blanket
multi-use inline exemption. Add an explicit member target to update/compound
syntax while keeping the current symbol-scratch update path unchanged. Its
certificate must bind original base/key UseIds, Get, optional ToNumeric, numeric
operation/RHS, Set, prefix/postfix result and each normal/exception port. Base
and key expression evaluation occurs once; Get/Set use that retained Reference.
The original source-property adapter still owns receiver/key-conversion phases.

Evaluation must index that joint action **before** temporary planning, seeded
only by the unmet member-call obligation. OriginalFlow/RecoveryFlow need the
individual Get/conversion/RHS/Set ports and the old/new result relationship;
substituting the whole AssignmentExpr commit is insufficient. Fresh verification
must reject a changed base/key, replayed getter, reordered RHS, write-before-
conversion, wrong old/new result, wrong catch or an extra observation. Native
printing is allowed only after those ports verify. No such admission or unused
certificate is added in this round. o36 remains parked at its exact old refusal.

## Final acceptance

Full build passes. Serial source-regression: **65/65**. Final targeted tests:
**13/13**, including both new CTest registrations. Requested sweep:
**260/273**. Its 13 failures all reproduce against freshly built **2e33dc77**
baseline targets: family_source_cli, tree_structure, try_tree, literal_construct,
mixed_literal, phased_literal, class_tree_source, literal_closure_source,
unguarded_literal_closure_source, derived_literal_receiver_source,
unguarded_derived_literal_receiver_source, phased_arm_selection_source and
try_tree_source. No expectation was weakened or changed.

Both 40-input cohorts complete with zero process failures. Previous flags:
**539 -> 540/620**; source: **595 -> 596/620**. **No regressions; no text drift
for any previously complete record.** Exactly o7/28433 changes its manifest
row in each contract. m37638/o36/36985 retains its PC378 (working352) Reference
refusal. Repeated RSS-measured outputs have byte-identical manifests.
Conservative four-child plus parent peak bounds: **165,036,032 bytes** (previous)
and **172,998,656 bytes** (source), below 1GB.

Raw commands/results, target before/after IR, all disassembly differences and
baseline-failure reproductions: `build/review/member-updates/`. No commit,
push or stash. The unrelated `sample.tar.gz` is unchanged.
