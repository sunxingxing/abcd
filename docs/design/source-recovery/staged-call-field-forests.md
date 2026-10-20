# Staged literal fields under a retained member Reference

2026-10-21; baseline `2a0ca2a8`. No new source-language premise.

## Original occurrence and source obligations

In o5/28303 m28799, `op102 PC262` reads the member into v526, before
`op104 PC268` allocates private object v528. Its definitions are op112,
op131, op175 and op237. `call241 PC642` consumes the saved callee through
Use275 and the exact receiver through Use276; Use277 consumes v528. This
is the saved member result crossing selected argument evaluation, not an
escaping object or a second member lookup. The constructor field retains
its constructor before two argument selections; the other fields include
a guarded capture/member call and a Boolean selection. Two later calls
in the same method have the same unmet semantic obligation, with separate
allocation/consumer identities.

`LiteralPlan` already proves private allocation, own-definition order and
non-escape. `LiteralFields` references those definitions rather than granting
an ordinary indexed/property Set fallback. It records the unique invocation,
original field membership and each field's ordered retained prefix roots.
Existing literal, constructor, selected-value, guarded-read and Reference
proofs retain their own obligations.

## Derivation and verification

The new census seeds only an unmet saved member Reference whose sole observed
argument is the private construction. One shared functional parent index
resolves inline operands, selected phi inputs, literal events and checked
guards. A shared effect/dominance index proves:

1. saved callee/receiver evaluation precedes allocation;
2. each field's retained snapshots and native conditional assignments precede
   its own definition, on exactly their original paths;
3. the final definition precedes the original invocation;
4. ordered and throwing members have the original invocation's catch state,
   without hidden exceptional copies;
5. each prefix root is consumed once; foreign effects or overlap refuse.

Pure retained conditional assignments are explicit anchors too. Ignoring
these because their VM operations have no language effect would read their
private phi slot before initialization. Prefixes remain assignments to the
existing private source symbols, sequenced inside that field value; they are
not replayed at later observers. The native callee is never re-read and no
`.call`/`Reflect.apply` lookup is introduced.

`VerifyLiteralFieldForests` rebuilds the census, parent forest, selected-arm
bindings, effect equations, ordinary inline intervals and catch membership
from the immutable graph and already independently checked input plans.
Nested registered compositions remain independently checked; their parent
forest cannot certify them by referring back to the containing literal.
All unsupported flow/order/escape/catch/ownership/resource cases keep typed
refusals. The allocation exclusion in phase composition is unchanged.

Checked member-base reads now use the same canonical-use proof as checked
arguments. The explicit receiver is an observation of that same Reference,
not another base evaluation. New unused selection candidates exposed by this
proof are not automatically committed: the selection verifier rechecks the
sealed choices and still censuses every owned logical branch/prefix/phi.

## Tree and recovery

Retained conditional assignments used by a field are constructed at their
verified original join, before that containing literal is lowered. Counting a
late predicate placeholder as the final selection would orphan the real arms;
the tree's complete ownership check caught this during development.
TreeFlow independently consumes each prefix, success/phi binding and field
value. Operand verification unwraps only the certified field sequences before
matching the stored SSA input. OriginalFlow and every RecoveryFlow projection
then verify the actual assignments, guards, invocation and own definitions.
For projected for-of control, selected-arm anchors use `SourceBlock`, not the
projection unit's numeric ID. No original edge or scope is default-mapped.

Indexes and event walks are prepaid. Each root/definition/operand is visited
once, plus sorting/shared dominance queries: O((V+E) log V), with no
per-construction whole-CFG scan. Exhaustion is a typed refusal.

## Captured `this` across a creator loop

In o36/11209, m12561 allocates one lexical slot at PC0, stores its receiver
in depth0/slot0 at PC12, and creates m11413 at PC187 inside the later loop.
The child reads depth0/slot0 at PC29. ScopeError value3 is
`kInitialization`. The creator has no local load demanding a cyclic
initialization proof, so the previous path indexed dominance but omitted
completed-store ranges used by capture queries.

That existing shared dominance path now also indexes the completed stores.
Allocation dominance, same-block store-before-use, environment/slot identity,
closure-write preservation and native allocation scope remain mandatory. This
is initialization evidence, not permission to hoist a per-iteration cell.
The capture passes; the next record refusal is m12194,
`PhasedMethodError::kControl > ExceptionControlError::kCrossEntry` (value3),
which is a separate exception/control ownership obligation.

## Evidence

See `build/review/staged-fields/` for raw diagnosis, exact-budget measurements,
cohorts, differential output and unnormalized SDK comparisons. The real
`fields` fixture exercises saved getters, constructor argument selections,
throwing arms, receiver identity, replacement during evaluation and inherited
setters. The separate early-TDZ fixture remains explicitly refused at the
pre-existing expression-ownership boundary; it is not counted as a published
differential. The captured-this fixture tests live updates after the loop. OLD refuses that fixture at ScopeError::kInitialization; NEW publishes it.


The o36 follow-up is m12194 `initCamera`. The test-only control probe reaches
`control_verify.cc`'s `shared != join` check: a compound decision's shared
arm is also its continuation. The current verifier requires a distinct owned
arm. The next slice needs an independently checked decision/continuation
projection, not more initialization evidence. `ExceptionControlError::kCrossEntry` is separate from
cell initialization: a scope certificate cannot authorize a second control
owner. A later slice should retain the original entry/edge and try-wrapper
identity, prove its enclosing scope/destination, and recheck that correspondence
in TreeFlow/recovery. No default scope or completed-store fact should bypass it.


## Original source comparison (evaluation only)

The original `TimeUtils.ets` is
`samples/applications_app_samples/code/UI/CalendarViewSwitch/casesfeature/calendarswitch/src/main/ets/customcalendar/utils/TimeUtils.ets`.
It is never an engine input. Its `byMonthDayForYear` pushes a four-property
object in each of the previous/current/next-month loops. For example:

```ts
week.push({
  dayNum: PREVIOUS_MONTH_DAYS[i],
  lunarDay: TimeUtils.lunarDayToChinese(i18nCalendar.get('date')),
  dayInfo: new DayInfo(month - 1 < 1 ? year - 1 : year,
                       month - 1 < 1 ? 12 : month - 1,
                       PREVIOUS_MONTH_DAYS[i], 0),
  isShowSchedulePoint: CommonData.SCHEDULE_ARRAY.includes(/* date */) ? true : false
});
```

The recovered field uses a retained constructor plus the original selected
parameter commits. The complete published method is in
`build/review/staged-fields/new-source-verified/o5/sources/calendarswitch/src/main/ets/customcalendar/utils/TimeUtils.mjs`:

```js
$value18.push({
  dayNum: $value15[$phi2],
  lunarDay: $local1.lunarDayToChinese($value19.get("date")),
  dayInfo: ($value38 = DayInfo,
    ($phi3 = month - 1 < 1 ? year - 1 : year,
      ($phi4 = month - 1 < 1 ? 12 : month - 1,
        new $value38($phi3, $phi4, $value15[$phi2], 0)))),
  isShowSchedulePoint: !!CommonData.SCHEDULE_ARRAY.includes(/* recovered date */)
});
```

The comments above abbreviate only this documentation comparison; the engine
output contains the complete date expressions. The current-month loop remains
native for-of. Its conditional lunar-day field now contains the checked member
calls directly. The final next-month loop uses its own distinct retained
constructor and phi bindings.


## Acceptance (final run)

- Full optimized-Debug/assertion build succeeds. Source-regression: **65/65**.
  New unit/integration/SDK tests: **5/5**. Touched-area sweep: **285/305**;
  all 20 failures are pre-existing. Eighteen reproduce through OLD CTest;
  `family_source_cli_test` and `class_tree_source_test` are not registered in
  that worktree's corpus configuration and reproduce by their same command
  with the OLD binaries. No failure assertion was relaxed.
- OLD is `2a0ca2a8`. Previous flags **541 -> 541 / 620**;
  language-source **598 -> 599 / 620**. No regression or text drift. Only
  o5/28303 (completion) and o36/11209 (next refusal) manifest rows differ.
- Node: staged fields **440** trace scenarios, including the native for-of
  fixture's `next`/`return` traces; captured loop **24** scenarios. Eight
  behavioral mutations are observed. The early-TDZ negative fixture remains
  typed/refused, separately from those comparisons. New production output
  passes `node --check`.
- SDK: **12 compile/disassemble invocations**, original/emitted at default
  and O0 for three fixtures; **18 per-method pairs**, six identical (entry
  methods), twelve with retained, unnormalized differences. Nothing is
  reported as bytecode equality after register/edge normalization.

The bytecode differences are source-normal-form bookkeeping: explicit
private `let` snapshots/phi commits add undefined initialization and register
copies; generated binding spellings change scope-name literals and HoleGuard
name strings; the captured-loop condition emits a Boolean materialization
before its break test; native for-of re-expands its original protocol while
those additional private slots shift registers, branch labels, IC indexes and
handler labels. The writer closure retains the explicit original guarded read
plus native PutValue's guard, whose extra check cannot newly fail under its
existing verified initialization/guarded-store proof. Own-definition kinds,
callee/receiver order, selected argument order and catch/close behavior remain
checked by the event proofs and Node traces. See each unnormalized method diff
in `build/review/staged-fields/roundtrip-verified/`.

Exact independent field-proof budgets (bytes/work) for 200/201/400/800 fields:
12,402,671/1,183,251; 12,464,497/1,189,150; 24,767,871/2,363,051;
49,498,271/4,722,651. Each exact bound passes; one byte or work unit less
refuses. The synthetic whole-pipeline fixture uses a 768 MiB cumulative
allowance; production defaults are unchanged. Actual unit peak RSS is
50,233,344 bytes. Final cohort runs report maximum CLI peaks of 42,991,616
(source) and 41,320,448 (previous) bytes; conservative four-child plus parent
bounds are 175,308,800 and 164,511,744 bytes respectively, below 1 GB.


The strict `grouping_bytecode.py` command was also run on the three
original/emitted fixture pairs after the same AST export adaptation.
As expected for source recovery rather than grouping-only edits, it reports
three token mismatches and three non-identical bytecode records at each
optimization level (no unverified compilations), with **3/3 Node syntax
checks passing**. Its exit status is nonzero, not an equality pass. The
per-method SDK report above expands those same differences; the six exact
entry-method matches and twelve body differences are kept without
normalization. This result does not weaken the zero-drift comparison against
OLD's already-complete outputs, which remains byte-identical.
