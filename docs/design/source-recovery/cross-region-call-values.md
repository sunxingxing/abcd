# Cross-region call values (2026-10-20)

Baseline: 041abdba. This round adds no source-language premise. Source text is
never an engine input. The names in the schematic constructs below describe
roles, not a recovered original spelling.

## Diagnosis before changing admission

All seven first refusals name the **saved member callee**, not an escaped array
or a receiver that could safely be turned into a value call. The chain is
`kExpression > kUnsupportedOperation > kReference > kNotInline > kRoot >
kCrossRegionValue`. Quoted IDs are the current Value IR, after verified copy
identity projection. PCs are original unless explicitly labelled working.

| Record / method / call | Exact operand and regions | Argument construct / missing proof |
|---|---|---|
| o18/22901 m24302 op93 PC212 | `op31@84 b0 -> v54; invoke93 b24 callee Use81=v54, receiver Use82=v51; argument Use83=v23` | A tree of boolean value selections. phi16/17/18/20/21/23 have predicate and retained keep-edge observations. Schematic `Button.enabled((a && b) || ...)`; requires the deferred **joint predicate/keep-edge** ownership proof. |
| o18/38531 m39900 op93 PC212 | Same operation/use/value numbering: member31@84 b0, call93 b24 | Same joint proof, not a second literal-arm instance. |
| o21/16320 m17082 op70 PC171 | `member8@21 b0 -> v28; invoke70 b10 callee Use70=v28, receiver Use71=v25; arguments Use72=v30, Use73=v32, Use74=v66` | `hilog.info(...)`: two optional property results in one concatenation. `invoke28@73 -> v45` and `invoke53@132 -> v61` each have a verified predicate snapshot. Branch32/57 own phi19/20, joins b5/b10. Existing domains stop at binary45 and invoke70 separately. Missing: **one eager event interval joining the already-proven selections and call Reference**. |
| o39/12680 m13322 op157 PC415 | `member120@330 b2 -> v75; invoke157 b11 callee Use161=v75, receiver Use162=v72; argument Use163=v20` | `owner.uiContext.px2vp(arg?.a.b() ?? fallback)`: optional branch126 yields phi19; its value feeds the tests and retained arm of branch146/phi20. The first selection has a phi-use consumer refusal. Needs joint predicate/keep-edge ownership in addition to interval composition. This round does not reinstate that withdrawn admission. |
| o7/28433 m28692 op58 PC166 | `member9@23 b0 -> v28; invoke58 b3 callee Use57=v28, receiver Use58=v25; argument Use59=v19` | Conditional **constructor results**, each wrapping a private object/array event forest. E.g. `o.create(flag ? new C({...}) : new D({...}))`. Literal-arm evidence alone does not certify the constructor/result/completion wrapper. |
| o36/36985 m37638 op137 PC378 (working352) | `member123@342 (working316) Before246/Commit247 -> v122`, aliases v123/v134; `invoke137 Before274 callee Use136=v134, receiver Use137=v120; argument Use139=v133` | `load127 -> member130 -> unary133@367 -> write134@369 -> v133` (aliases preserve v132). A prefix numeric update/write result, schematically `AppStorage.setOrCreate(text, ++state.p)`. Needs numeric update/reference/result completion composition, not a selection admission. |
| o5/28303 m28799 op231 PC620 | `member183@488 Before366/Commit367 -> v592`, alias v630; `invoke231 Before462 callee Use263=v630, receiver Use264=v590`. `property227@610` base `Use257=v623` resolves through the earlier guarded value. | `savedArray.includes(...)`: string/boolean selections plus a retained guarded capture read: read20@56 / guard21@58. `GuardedCallError::kUse` rejects **Use257 of property227**, not Use263. The single-use guarded-arm proof cannot replay or migrate this retained observation. Needs retained guarded-snapshot correspondence. |

An ordinary `const fn = o.m` does not preserve the member Reference's receiver.
Calling `fn(args)` loses `this`; looking up `o.m` again changes getters and
callee replacement. No `.call`/`.apply` or builtin-identity premise is introduced.
The member spellings above were independently resolved from the original
method/pool identity by `ResolvePoolReference`/`ReadStringEntity`; see the
scratch `member-names.txt`. Argument snapshots remain available where the existing scope/placement proof
supports them; this round only moves a subtree when its full event interval fits.

## Implemented proof

A fallback pass runs **after** ordinary normal-flow logical planning and only
for a member Reference still unmet there. A shared canonical-use index with path-compressed forest roots proposes
eager single-consumer ancestors and groups complete, already-committed
`SelectionRegion`s by their eventual call. It does not grant a selection a new
consumer, resolve a phi's predicate/keep-edge observations, or remove literal
allocation exclusions. Existing successful placements are untouched.

`LogicalReferenceRoute{call, branches}` retains original branch and phi identity.
The existing Domain reverse interval proof composes all of its selections,
snapshot assignments and eager operands together. A source event is never
skipped merely because a child was materialized by an earlier domain. Owned
subtrees must fit consecutively and uniquely, in order: callee/base/key lookup,
earlier arguments, selected tests and conditional arms, later arguments,
invocation. Unowned effects cause refusal; they are not hoisted or discarded.
The original predicate snapshot's first assignment and subsequent reads remain
distinct. Each loop/call activation and arm executes the original number of times.

Verification independently rebuilds control selections and the canonical-use
index from the immutable graph, checks the claimed route against that census,
and runs Domain's **forward** event/ownership equations. Original selection
arms/phi edges, CallPlan's exact callee/receiver check, TreeFlow, OriginalFlow and
recovery verification remain mandatory. This is normal flow: throwing events
still propagate to the caller. Phased catches are not admitted by this pass;
their catch-state and guarded-call checks remain unchanged.

Sole-entry ordinary corridors are indexed once, with component/rank identities.
Thus inserting a copy or splitting a normal corridor does not require a new
recognizer, and failed reachability proposals do not walk the CFG repeatedly.
CSR ownership lists and an explicit flat route keep additional work O(V+U+E),
apart from existing sorted control/use indexes, with O(V+U+E) admitted storage.
Buffers, route records and branch lists are charged before allocation. Exhaustion
returns the existing typed work/resource refusal. Missing consumer, frame,
escape, order, catch-state, receiver and correspondence obligations stay closed.

## Validation

See the round entry in status.md for final counts. Scratch evidence is under
`build/review/cross-region-calls/`. Positive real es2abc fixtures cover sequential
optional and conditional argument subtrees, early argument reads, callee getters
and replacement, receiver identity, null/undefined, per-step throws and coercions.
A retained predicate/keep-edge fixture deliberately keeps its typed refusal.
Tests mutate route membership/order, call/operand identity, consumption and
observable-event coverage, and exercise 200/201/400/800 width and call count
with exact work and byte boundaries. No corpus identity enters production code.

## Compiler comparison

The pinned compiler is SHA-256
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
The fixture manifest records full commands (`--module --merge-abc`, default
and `--opt-level=0`, identical record and source-file identities). The engine's
emitted fixture was recompiled and compared using the unmodified `run`, `sha`
and `assembly` helpers from `tests/tools/grouping_bytecode.py`. The only source
adaptation was spelling the SDK's string-named export aliases as identical
identifier aliases. No registers, instructions, branches or handlers were
normalized away.

At default optimization, the **entire disassembly is byte-identical**: all three
exported methods and `func_main_0`, including metadata. At O0, `conditional` and
`func_main_0` are identical. `optional` and `earlier` each have exactly six extra
instructions: two `ldundefined; sta` initializations for the explicit private
snapshot locals and one extra `sta` at each of their original child-call result
Commits. These two added private locals also shift the compiler's temporary
register allocation. The diffs retain the same property accesses, two separate
child invocations, nullish decisions, coercions and final receiver call in the
same order; neither method has a user handler. This is explained semantic
correspondence, **not** a claim of strict O0 bytecode equality. Default compilation
independently removes those unobservable private slots and produces exact code.
Raw per-method results and non-normalized diffs are in
`build/review/cross-region-calls/bytecode-verified/`.

Final result: **o21/16320 completes under both flag sets**; all other six first
causes and locations remain unchanged. The cohorts are **535/620** and
**587/620**, with no previously-complete text drift. Full verification and the
baseline-confirmed sweep failures are listed in status.md.

| Scale | Calls: exact admitted bytes / work | Width: exact admitted bytes / work |
|---|---|---|
| 200 | 17,177,890 / 4,942,506 | 7,753,159 / 2,182,699 |
| 201 | 17,263,753 / 4,967,494 | 7,791,856 / 2,193,402 |
| 400 | 34,350,490 / 9,981,552 | 15,492,559 / 4,409,385 |
| 800 | 68,695,690 / 20,153,586 | 30,971,359 / 8,912,631 |

Every reported exact boundary succeeds; one fewer byte or work unit refuses
with the corresponding typed resource/work cause. The large test budget is
explicitly test-only; production budgets are unchanged.

## Joint selection ownership (2026-10-20, baseline 1f4999b8)

Fresh original-value census: o18 m24302/m39900 `member31@84 -> v54`
feeds `call93@212 callee Use81=v54, receiver Use82=v51, argument Use83=v23`.
`phi17` is read by `ToBoolean50@124 Use46` and kept unchanged by
`phi20 Use115/edge19`; `phi20` is itself tested and kept by the next selection.
The two methods have identical def/use identities. The existing discovery stops
at a multiple-observation phi before building the outer Boolean selections.
For o39 m13322, `member120@330 -> v75` feeds `call157@415 Use161`;
`phi19` feeds pure strict null test145@387 Use148, strict undefined test150@397
Use154, and kept `phi20 Use183/edge15`. `phi20` is argument Use163.
The saved member is the cross-region value in every case; moving/re-reading it
without joining these exact argument events would change receiver/getter order.

### Implemented joint-use boundary

`JointSelectionUse` records the original phi, exact kept UseId, ordered pure
predicate UseIds and unmet member-call owner. A canonical occurrence census and
path-compressed value-use forest discover candidates once per method. The
builder does not turn this census into general inline permission: fresh
`SelectionLogical` truth equations must cover **every** predicate occurrence,
identify the same retained value and match its original incoming edge. The
ordinary reverse interval builder then proves callee -> argument events ->
invocation; forward verification recomputes that interval independently.
Records are committed only with a successful reference route, and verification
requires every record to belong to that route. Extra observations, changed
kept values, effects, wrong owners and missing records remain refusals.

The o18 RHS contains consecutive Boolean selections (the two Boolean-value
normalizations of `!!field`). Their prior selected phi is exclusively consumed
by the next branch's pure evaluation ancestry. The conditional arm view now
retains those selections as typed arm prefixes/forwarded children. Every
intermediate header must contain only that predicate's pure evaluation forest;
an unrelated effect cannot be hidden in the contracted arm. Child forwarding
commits atomically with the enclosing route. This reuses the existing prefix,
phi-edge and truth-event correspondence; it does not alias distinct phis or
restore the withdrawn global Round-46 consumer admission.

Empty jump corridors use a shared, prepaid index and sole-entry/sole-successor
edges, not block adjacency or a same-block requirement. Every new traversal is
iterative. Occurrence/owner tables and work stacks are charged before allocation;
path compression, disjoint membership and sorted certificate lookup keep the
bound O((V+E) log V), including copied and split variants. No printer or source
contract changes are involved. The original kNotInline, effect/order, snapshot,
catch, escape and ownership refusals remain in force outside the sealed route.

### Remaining nullish projection

For o39, the raw joint census is valid but not sufficient: region146 contains
partial child151 with the **same merged phi20** and a shared constant fallback.
The root has no direct retained-snapshot arm, so `ProjectSelectionLogicals`
correctly does not publish `SelectionLogical`; phi19 still has `kPhiUse`.
The first published refusal remains saved property120/call157 PC415.

The next proof needs a shared-phi decision summary: partition the tested value
by the existing four truth domains, map **every** original fallback/keep edge
(including split shared arms) to its original value and event, and bind every
scalar test to one native nullish-left evaluation. TreeFlow/OriginalFlow and
recovery must retain those individual incoming edges rather than confuse a
Boolean control root with the selected value. No new language premise is
needed; this round deliberately keeps that representation gap refused.
The optional/nullish fixtures and their optimized inputs assert the actual
CallReferenceError::kNotInline rather than being counted as recovery passes.

### Published code versus source oracle

Paired ArkTS was inspected only after recovery and was never supplied to the
engine. Tcp2TwoWayTls.ets:186 and TwoWayTls.ets:186 both contain
`.enabled(!!this.ca && !!this.cert && !!this.privateKey)`.
The recovered m24302/m39900 callback in both records is:

```js
(elmtId, isInitialRender) => {
  Button.createWithLabel({id: 16777226, type: 10003, params: [], bundleName: "com.samples.Socket", moduleName: "entry"});
  Button.onClick(() => {
    $local0.connect2Server();
  });
  Button.enabled(!!$local0.ca && !!$local0.cert && !!$local0.privateKey);
  Button.width(70);
  Button.fontSize(14);
}
```

`joint_selection_use_test` checks canonical copies, register permutations,
empty split corridors, predicate/keep/call and missing/duplicated certificate
mutations, an extra observation, changed kept payload, an intervening effect,
logical operand swaps through independent tree/recovery checking, and exact
byte/work boundaries. Both many-call and one-call wide chains are exercised at
200/201/400/800. The real fixtures pin SDK commands/hashes in their manifest.
Published fixture output is compared against Node on values, getter/call order,
exception constructors, noncallable timing, receiver identity and argument-side
callee replacement. Strict recompilation uses grouping_bytecode.py's compiler,
hash and disassembly helpers without register/control/exception normalization.

Final cohort comparison against wtbase **1f4999b8**: previous flags
535 -> 537/620; V5 587 -> 589/620. Both have zero regressions, zero changes to
previously-complete text, and exactly the two o18 status/diagnostic changes.
HardKeyUtils (the withdrawn Round-46 regression) remains complete and identical.
All 40 inputs finish. Measured largest child RSS: 43,204,608 bytes; conservative
four-worker bound: 172,818,432 bytes. Both new records pass node --check.

Full build and serial source-regression passed (65/65); new tests pass 2/2.
The independent Node matrix has 21,000 scenarios, explicitly encoding undefined,
NaN, -0 and object identity and rejecting accidental valueOf evaluation. The
three published fixture outputs recompile with the pinned es2abc SHA-256
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`:
O0/default, 6/6 full disassemblies and 12/12 methods identical, with no
normalization besides the informational source-binary comment. String-named
export aliases are adapted to identical identifier spellings for SDK parsing;
there are no instruction, register, branch, handler or metadata differences.
Exact commands and disassemblies are in `build/review/joint-selection/bytecode-verified/`.


Requested final sweep: **248/268**. Remaining failures are pre-existing:
`family_source_cli_test`, `tree_structure_test`, `try_tree_test`,
`literal_construct_test`, `mixed_literal_test`, `phased_literal_test`,
`class_tree_source_test`, `literal_closure_source_test`,
`unguarded_derived_cells_source_test`, `unguarded_literal_closure_source_test`,
`derived_literal_receiver_source_test`,
`unguarded_derived_literal_receiver_source_test`,
`unguarded_captured_receiver_source_test`, `unguarded_forwarded_receiver_source_test`,
`unguarded_reversed_captured_receiver_source_test`,
`unguarded_reversed_forwarded_receiver_source_test`,
`unguarded_reversed_initialization_captured_receiver_source_test`,
`unguarded_reversed_initialization_forwarded_receiver_source_test`,
`phased_arm_selection_source_test`, `try_tree_source_test`.
The phased-arm case was additionally reproduced on wtbase 1f4999b8 with the
same original line20 assertion (`0 !== 2`). Its older evidence is also retained
in status.md; no existing failure assertion was loosened.

The only stale refusal expectation found in the first sweep was
`selection_consumers_cli_test`'s `target.send(!!x.a && !!x.b && !!x.c)`.
The refreshed test requires full recovery under both contracts and compares
22 additional receiver/getter/short-circuit/throw scenarios to the original
source. Four receiver/operator mutants are observed in addition to the eight
existing mutants and 42 existing differential executions. The exact two
cohort completion expectations in `evaluation_failure_cli_test` were refreshed
to require full manifest/CLI source equality. The final sweep passes both.

Evidence directory: `build/review/joint-selection/`; final cohorts are
`verified-source` and `verified-previous`, SDK comparison is
`bytecode-verified/summary.json`, exact budgets are in
`new-tests-detail.log`, regression and sweep outcomes in
`source-regression.log` and `sweep-final.log`. No commit/push/stash was made.

## Shared-phi nullish calls (2026-10-20, baseline f1eb2418)

### Fresh boundary and implementation

Original m13322 has `member120@330 -> v75`; `invoke157@415` consumes
`Use161=v75`, receiver `Use162=v72`, argument `Use163=phi20`. The optional
selection `branch126 -> phi19` precedes `strict-null145@387 Use148=phi19`
and `strict-undefined150@397 Use154=phi19`. `branch146` owns the complete
phi20 and `branch151` owns a partial subtree of that **same** phi. The retained
leaf is `Use183/edge15=phi19`; the two other original incoming edges carry
separate pure zero constants. This is a value selection, not an extra read of
the optional call or a Boolean result replacing it.

`ProjectSelectionLogicals` now derives a shared-phi view only from the existing
unmet-Reference `JointSelectionUse` seeds. Its explicit stack partitions the
original selection tree into the four existing truth domains. Every scalar
must be a pure equation of the same retained ValueId. Every leaf must retain
that exact value/edge or supply the same canonical constant/retained binding.
The retained domains must be exactly non-nullish; fallback domains exactly
null/undefined. An effectful or unequal fallback, extra observation, unrelated
event, changed retained input or incomplete path keeps the refusal.

`SelectionLogical` retains the root, original keep occurrence, shared fallback
and disjoint descendant list. No original region/phi/edge is removed. Each
predicate retains its own evaluated-domain and true-domain sets, including the
child's non-null arrival. The expression forest emits one `LogicalExpr(??)`
and combines the proven pure test and fallback origins. It never first builds
unused duplicate syntax. TreeFlow independently recomputes the view from the
original graph, visits every original child and selects every actual phi edge;
OriginalFlow/recovery compare each original scalar domain/occurrence against
that single source evaluation. These are normal-flow Reference routes; no
phased catch/guard admission or new language premise is introduced.

After PC415 was composed, the next failure was `member112@310 -> v67`, the
callee of `invoke165@442` (`Use172=v67`, receiver `Use173=v64`, argument
`Use174=v92`). `invoke157 -> v89` feeds `binary160@428 -> v92`. Both members
are saved before the selections. The canonical one-use ancestry indexes now
continue through an inline member invocation to the outer saved member owner,
retaining the outermost actual Reference if the chain ends at a non-Reference
statement. One existing Domain interval then proves both lookups, the selected
argument, both calls and subtraction in source order. This is not permission to
re-read a callee, invoke via `.call`, or discard intervening effects. Verification
rebuilds that ancestry and its forward event equations independently.

All added scratch arrays/stacks and result vectors are admitted before
allocation. Shared-region subtrees are disjoint; indexes and explicit walks are
linear apart from existing sorted ownership lookup, O((V+E) log V). The fallback
runs only on an unmet Reference. Successful old plans are unchanged.

### Result and source oracle

**o39/12680 completes under both contracts.** The source oracle is
`samples/applications_app_samples/code/DocsSample/ArkWeb/ArkWebMenu/entry/src/main/ets/pages/WebContextMenu.ets:117`,
inspected only for evaluation after engine recovery. Its callback statement is:

```ts
this.offsetY = Math.max(this.uiContext!.px2vp(event?.param.y() ?? 0) - 0, 0);
```

The emitted m13322 callback is:

```js
(event) => {
  if (event) {
    $local0.result = event.result;
    console.info("x coord = " + event.param.x());
    console.info("link url = " + event.param.getLinkUrl());
    $local0.linkUrl = event.param.getLinkUrl();
  }
  console.info(TAG, "x: " + $local0.offsetX + ", y: " + $local0.offsetY + "");
  $local0.showMenu = true;
  $local0.offsetX = 0;
  $local0.offsetY = Math.max($local0.uiContext.px2vp(event?.param.y() ?? 0) - 0, 0);
  return true;
}
```

### Remaining cases, deliberately not admitted

- **o36/36985 m37638 original PC378 / working PC352** remains the same
  CallReference `kNotInline > kRoot > kCrossRegionValue`. Original saved
  `member123@342` (working316) produces v122, copied to v123/v134, consumed
  by `invoke137 Use136`, receiver Use137=v120. Its second argument
  `Use139=v133` aliases the numeric result v132. In working IR:
  `lexical127@327 -> v126`, copies v127/v128, `member130@333 Use128=v128 ->
  v129`, copies v130/v131, `unary133@341 Use131=v131 -> v132`, then
  `write134@343 Use132=v127, Use133=v132`; result copy135 supplies the call.
  The raw unary operator is **kIncrement** (not a guessed compound or postfix
  update); the incoming environment address is depth0/slot0. The returned
  argument aliases the new numeric result, so this is the prefix-result case.
  The assignment-result correspondence already exists: write134 is inline at
  that argument. The captured base has **both** the Get and Set observations.
  The present `UpdateExpr` represents only a private numeric **symbol**, not a
  member Reference; neither duplicating the lexical read nor absorbing just
  the write proves native prefix/postfix semantics. The next contained design
  is a MemberUpdate reference certificate: original base/key occurrence once,
  Get -> ToNumeric/update -> Set successful Commit, old/new result identity,
  and all abrupt exits, consumed atomically by phase composition and mirrored
  in tree/recovery. Until that reference proof exists the current refusal is
  correct. No update opcode or sample shape has been newly admitted.
- **o7/28433 m28692 PC166** remains the same Reference refusal. Saved
  `member9@23 -> v28` feeds `invoke58@166 Use57`, receiver Use58=v25,
  argument Use59=phi19. The arm ending at `construct35@98` returns v97;
  the other arm ends at `construct55@158` with v50. Their private object and
  array arguments have their own definitions, but **the constructor result,
  not either allocation, is the phi input**. `LiteralArmView` certifies a
  literal allocation/final-value forest; it does not own the enclosing
  constructor invocation and exception/result commit. The next proof must
  extend an arm event view through an independently verified ConstructCall
  (callee/arguments -> constructor Before/Commit/exception -> selected value),
  without changing the existing allocation exclusion. It cannot be inferred
  from the nullish test view implemented here.
- **o5/28303** was not pursued; its retained guarded snapshot proof remains
  separate. The corpus check requires its existing diagnosis to stay intact.

### Tests and decisive compiler comparison

`joint_selection_use_test` covers direct and nested Reference owners, copies,
register shifts and split corridors. Mutations remove/duplicate a joint proof,
change its owner/predicate/kept edge, add an observation or effect, change a
fallback constant, change a nullish domain, and swap native logical operands.
Fresh re-planning rejects unequal fallback/domain mutants; no stale proof alone
is counted as that negative result. Many calls and a wide chain exercise
200/201/400/800 with exact work and byte boundaries (one less refuses).

The new `nullish_call_values_source_test` uses the existing pinned optional
method/property/nullish-chain ABCs and two new real es2abc fixtures: an optional
method with an argument, and nested saved member calls. **5 O0 inputs publish**;
the 5 optimized-input encodings keep their exact Reference refusals. Published
methods compare **17,280** Node scenarios, including null/undefined, other falsy
values, NaN/-0, BigInt/Symbol/object identity, getter/call/coercion throws,
noncallable timing, receiver identity and argument-side callee replacement.
All 10 receiver/operator mutants are observed. The prior Boolean matrix still
compares 21,000 scenarios; its three new nullish publication checks are backed
by the separate differential, not weakened refusal assertions.

The emitted methods are compiled with the pinned SDK at default and O0 using
`grouping_bytecode.py`, compiler SHA-256
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
All **10/10 per-method comparisons match exactly**, with zero allowed
instruction/register/branch/handler/metadata differences. Both sides use the
same test-only function/export wrapper, retaining original body/signature
text; only the SDK's identifier-valued string export spelling is adapted
symmetrically. The tool's separate grouping-only token check reports 5 failures
because recovered parameter names differ from original names; this is a
recovery round, not a claim that identifier changes are grouping removals.
That exit status is not hidden or normalized. Bytecode and Node checks pass
5/5 at each optimization; raw reports/commands/diffs are under
`build/review/remaining-calls/bytecode/`.

Final build/cohort/test evidence is recorded in the current status entry.

Final acceptance: full build; **65/65** source-regression; **7/7** targeted;
requested sweep **208/214**. Its six failures are all reproduced with freshly
linked f1eb2418 baseline executables: family_source_cli, tree_structure,
try_tree, class_tree_source, try_tree_source and phased_arm_selection_source.
Both 40-input cohorts have no process failures: previous **538 -> 539/620**,
source **594 -> 595/620**, no regressions and no previously-complete text drift.
Exactly o39/12680's manifest row changes in each. Newly emitted records pass
node --check. Final-verifier-build manifests are byte-identical to all 80
RSS-measured manifests. Conservative four-child+parent bounds are
165,658,624 bytes (previous) and 175,620,096 bytes (source).

| Nullish scale | Nested calls: exact bytes / work | Wide chain: exact bytes / work |
|---|---|---|
| 200 | 22,677,992 / 5,387,988 | 8,995,150 / 2,022,662 |
| 201 | 22,791,333 / 5,414,922 | 9,039,733 / 2,032,429 |
| 400 | 45,346,192 / 10,873,627 | 17,911,750 / 4,082,774 |
| 800 | 90,682,592 / 21,949,101 | 35,744,950 / 8,257,111 |

Each boundary succeeds; one less byte/work unit refuses. The independent
verifier checks joint-certificate identity/ranges **before** any region view
indexes them; out-of-range value/keep mutations now fail with typed ownership
rather than relying on trusted builder output. No commit, push or stash.
