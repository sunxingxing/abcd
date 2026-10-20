# Member-update References and constructor-arm events

The current retained-member round (baseline **78278c38**) is recorded below in
[Native retained member References](#native-retained-member-references-2026-10-21-baseline-78278c38).
The following first checkpoint is historical.

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

## Native retained member References (2026-10-21, baseline 78278c38)

The later member-update round consumes the missing Reference evidence.
**o36/36985 completes under both flag sets.** The original evidence above is
unchanged: load127/v126 has Get130/Use128 and Set134/Use132 as its two real
observations. Its source occurrence now belongs to one `MemberMutationExpr`;
Set's base/key inputs are retained-reference wires, not additional source reads.
The new value v132 reaches Invoke137/Use139 only after Set succeeds.

`member_update.{h,cc}` discovers only from an **unmet member-call Reference**.
Normal SSA uses the shared must-reach effect equations to join saved callee,
mutation and invocation. Phased SSA composes a separately verified native atom
into the existing expression composition. Existing successful calls, numeric
scratch lowering, literal allocation exclusions and conditional admission stay
unchanged. A second composition preserves earlier registered roots rather than
replacing them; the original empty-registration path retains its old storage.
No sample names, PC/register layout, same-block rule or opcode sequence chooses
admission.

The certificate retains canonical base/key ValueIds via their original UseIds,
Get, optional explicit ToNumeric, arithmetic, Set, original result ValueId and
its sole argument occurrence. One prepaid use census and functional-parent
index collect existing input expression forests. One shared EffectOrder proves
that the required events concatenate on every normal path; unrelated effects,
divergence or unfinished activation re-entry cannot disappear. Scalar aliases,
register permutations and pure block splits do not alter these equations.
Every intrinsic's operand roles, purpose and original owner are checked as
well. Foreign observations, a different receiver/key, extra result consumer,
intervening effects and catch staging keep the existing refusal.

Each port keeps its original successful Commit and exceptional-exit/handler
identity. All throwing events in a moved input forest must retain the original
catch context and have no unrepresented catch copies. `VerifyMemberUpdates`
re-derives the certificate and all membership/input/result/wire tables from the
unchanged graph; supplied placements and certificate fields are compared, not
trusted. Removing the certificate cannot leave active wire/ownership tables.

`MemberMutationExpr` has a MemberTarget, a typed increment/decrement or compound
binary operator, and an optional RHS. Its owned children are base, computed key
and RHS, each evaluated once. Prefix returns the new value; postfix returns the
**converted numeric old value**, never the old object. Compound assignment gets
before evaluating its RHS and returns the stored value after successful Set.
The syntax/printer handles dot/computed targets and assignment precedence;
symbol-only UpdateExpr and all previously successful statement/parameter forms
keep their old lowering. Logical assignments are not admitted by this proof.
The production seed is the unmet call Reference; this is not an unconditional
replacement of every successful numeric lowering.

TreeFlow freshly re-verifies the certificate, actual source target/key/operator,
RHS and strictness, then publishes individual OriginalFlow actions.
RecoveryFlow reconstructs one source Reference with separate Get/Set access
views, an explicit ToNumeric action for updates, arithmetic and successful Set.
An original INC/DEC that includes conversion maps to conversion Before through
arithmetic Commit; an explicit TONUMERIC gets its own port. Arithmetic after
ToNumeric cannot execute user code; allocation-failure observability remains
controlled by the existing resource policy. Original result/call correspondence
keeps the returned old/new value separate from Set's success. Every recovery
projection must retain the same source target/operator and individual physical
ports; replacing the node withdraws them. No internal numeric value receives a
fabricated ExprId or source variable.

### Runtime, strictness and premise boundary

The source action order follows ECMA-262 2025 [Update Expressions, 13.4](https://tc39.es/ecma262/2025/multipage/ecmascript-language-expressions.html#sec-update-expressions)
and [Assignment Operators, 13.15.2](https://tc39.es/ecma262/2025/multipage/ecmascript-language-expressions.html#sec-assignment-operators-runtime-semantics-evaluation).
Base/key **expressions** evaluate once. Under the repository's existing 2025
Reference adapter, an unresolved object key converts separately at Get and Set;
that is not a second key-expression evaluation. See
[source-property-references.md](../../reference/source-property-references.md).
Node v25.7 confirms both key conversions, including a key that changes on the
second conversion or throws there.

Pinned `ets_runtime/ecmascript/interpreter/interpreter-inl.cpp:3037,6442,4979,4860`
implements named Set/Get, INC and TONUMERIC. The name/value slow store adapters
at `ecmascript/interpreter/slow_runtime_stub.cpp:816,838` delegate to
`ecmascript/stubs/runtime_stubs-inl.h:664,773`, whose SetProperty calls pass
**true** for strict mode. The native tree must independently be strict.
Own-definition stores and super/private References are not ordinary member
updates. No new premise is introduced: the selected existing arithmetic/source
numeric and resource policies still apply; no numeric `+ 1` approximation or
new initialized-capture claim is made.

### Publication and source oracle

| Target | Before | After |
|---|---|---|
| o36/36985 m37638, original378 / working352 | kNotInline > kRoot > kCrossRegionValue | complete |

Paired source is evaluation-only. The relevant original callback in
`PhotoSameSource/entry/src/main/ets/views/ModeComponent.ets:157` is:

```js
async () => {
  if (this.sceneMode === camera.SceneMode.NORMAL_PHOTO) {
    Logger.info('capture onClick');
    AppStorage.setOrCreate('captureClick', ++this.captureClickFlag);
    CameraService.capture();
  } else {
    await CameraService.startVideo();
    this.isRecording = true;
  }
}
```

Engine output, with the already-verified condition snapshot/inversion retained:

```js
async () => {
  const $value0 = $local0.sceneMode;
  const $value1 = camera.SceneMode.NORMAL_PHOTO;
  if ($value0 !== $value1) {
    await CameraService.startVideo();
    $local0.isRecording = true;
  } else {
    Logger.info("capture onClick");
    AppStorage.setOrCreate("captureClick", ++$local0.captureClickFlag);
    CameraService.capture();
  }
}
```

Both emitted records pass node --check. An independent extraction/execution of
this actual engine callback against the original callback passes **140** async
value/exception/effect/microtask scenarios. Original source is never an engine
input.

### Tests, recompile differences and remaining limits

`member_update_test` checks derivation, re-verification and fresh TreeFlow/
RecoveryFlow ports. Mutants alter the target, input role, arithmetic, old/new
result, consumption wire, placement, port, exception/handler or retained source
Reference. Copies, block splits, register permutation, named/computed prefix and
postfix cases pass. Exact prepaid verifier budgets and one-less-byte/work
refusals at 200/201/400/800 are recorded in
`build/review/member-native/unit-result.log`.

| Width | Verifier bytes | Verifier work |
|---:|---:|---:|
| 200 | 2,284,927 | 439,989 |
| 201 | 2,296,346 | 442,188 |
| 400 | 4,568,727 | 879,789 |
| 800 | 9,136,327 | 1,759,389 |

Real es2abc fixtures and commands/hashes are in
`tests/integration/fixtures/native_member_updates/manifest.json`.
Default and O0 publish thirteen ordinary functions each; O0 also publishes two
catch-bearing factories. The optimized catch fixture retains
`PhasedMethodError::kRegion > ExceptionPlanError::kLayout >
CatchRegionError::kHandlerShape`; 78278c38 independently has that same refusal.
This round does not expand catch-region admission.

Node differential: **5,852** cases and **8** observed source mutants. Coverage
includes prefix/postfix, decrement, +=/-=/*=, statement/assignment results,
captured and nested bases, mutable capture replacement during Get, callee
replacement/noncallability, key/getter/setter/valueOf/ToPrimitive order, second
key conversion and failures, strict write failures, Number/BigInt/Symbol.

The unmodified `grouping_bytecode.py` compares 28 symmetric function wrappers
at default and O0: **8 exact / 48 differences / 0 unverified**. No allowance,
register normalization or hidden mismatch removal is used. The grouping-only
token checker correctly rejects all 28 parameter-renamed/recovered pairs; these
are source-recovery comparisons, not grouping-only passes. All 28 Node syntax
checks pass. Every difference belongs to the following inspected categories:

| Methods | Difference and semantic correspondence |
|---|---|
| makePrefix, makePostfix, makeDecrement, makeCompound, makeSubtract, makeMultiply, makeMutable, makeCaught, makeCaughtCompound | Recovered captured formal is initialized into a source `let` before closures can escape. Recompile adds unreachable Hole guards and their name strings/constant offsets; O0 also materializes the parameter/local through extra register copies. Native Get/ToNumeric/arithmetic/Set/call order and receiver remain unchanged. Mutable writer additionally has a dead pre-store guard and return-value copies. Catch protection remains around the same live source actions, including the dead guard. |
| prefix, compound, multiply, statement | Default matches exactly. O0 retains existing private scratch/assignment/key snapshots and their register copies; the body effects and successful result remain the same. These already-complete lowerings are intentionally unchanged. |
| postfix, makeCaptured | Existing lowering retains extra private numeric scratch operations for old-number conversion and value publication, plus inert binding reads/copies/initializers (and dead captured-let guards). Extra arithmetic has no user callback; resource differences remain under the existing exclusion. Numeric result, user Get/Set count, order and receiver match; these are not claimed as new native-form support. |

Compiler SHA-256: `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
Disassembler SHA-256: `a82e0c92a6726eb5c515f96c1731984e2a3938a6ce98dc540c7b5e7c08860676`.
Detailed raw diffs are under `build/review/member-native/roundtrip-accepted/`.
No claim of full bytecode identity or Ark runtime execution is made.

A full baseline syntax census finds **9** retained numeric scratch member
lowerings in **7/596** complete source-contract records, and **8** in **6/540**
previous-contract records. Typed/lazy syntax is shadowed only in the test census
and identifier-valued string export aliases are adapted; every complete record
is counted. These potential native spellings are intentionally untouched, not
additional admissions. A broader syntactic count also finds 249/248 direct
member/binary-store spellings (source/previous); together these touch 26/24
complete records. Those spellings are not proofs that base/key evaluations may
be merged; they are explicitly potential cleanup candidates. All stay
byte-identical. The registries are `build/review/member-native/census-*.json`.

Additional work/storage is one shared use/owner/effect index per method plus
linear certificate/event/input tables: **O((V+E) log V)** admitted work, no
per-mutation whole-CFG scans and no recursive proof walks. Every buffer is
charged before allocation. Complete-record text stays byte-identical.
