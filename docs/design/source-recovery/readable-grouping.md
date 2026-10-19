# Readable grouping — slice 1a

Grouping-only text changes are authorized. `PrinterPolicy::kReadableV1` names the
classification policy; it is not a LanguageSource premise. This slice keeps all
indirect `(0, value)` wrappers and all blocks. It does not change an external
cache-key schema; policy/cache integration remains a later boundary.

## Printed contexts, not an occurrence adapter

`TreePrinter::PrintOriginalExpression` consults `ReadableGrouping` after its
value/name/boolean projections. Sequence, update, conditional and assignment
emitters no longer add a second unconditional pair. Their actual operand context
selects grouping. Uncovered contexts remain conservative. Existing function and
class name-inference envelopes remain paired and unchanged.

| Covered context | Required grouping |
|---|---|
| Call/constructor argument, array element/spread, assignment RHS, conditional arm | AssignmentExpression: keep a sequence grouped |
| Sequence left / right | Expression / AssignmentExpression; preserve association |
| Conditional test | Keep conditional, assignment and sequence operands grouped |
| Binary/logical operand | Preserve precedence and equal-precedence AST association; preserve nullish/logical restrictions |
| Member/call base | Keep low-precedence bases and update expressions, including `(x = []).push(...)` and `(x++).p` |
| Unary sign applied to prefix update | Keep grouping to prevent `+++` / `---` token merging |
| Unknown/monostate | Keep the conservative parentheses |

Object literal values use the existing AssignmentExpression emission when the
parent now supplies that precise context. Snapshot conditional arms likewise use
AssignmentExpression. No declaration, scope, call reference or evaluation order
is changed. Counting and real sinks run the same action stream; expression and
method positions are measured from that stream, without offset patching.

Functions/arrows and their name inference, statement-start/directive hazards,
for-init `[In]`, template tags, optional-chain termination and constructor-spine
special cases are **not newly relaxed**. Unary/default-value and projected
nullish/boolean forms retain their existing conservative envelopes. The syntax
library tests some of these grammar rules, but that is not a claim that all of
them are enabled in production.

## Proof and resource boundary

`GroupingPlan` is an occurrence-indexed, independently testable library.
`VerifyGroupingPlan` recomputes syntax summaries and decisions rather than
trusting supplied facts. Its explicit postorder stack computes left-edge hazards,
string/directive and `in` summaries in O(nodes + edges + occurrences), using
prepaid `TreeCheckBudget` buffers. The production subset uses constant-time
classification at the final print position: no new subtree scans or allocated
per-use plans. Tests exercise decision/fact mutations and exact byte/work
boundaries at 200/201/400/800 width and depth.

## Reusable acceptance

`tests/tools/grouping_tokens.mjs` uses pinned Acorn 8.15.0 (MIT), via Node's
`--expose-internals`, to identify actual ParenthesizedExpression delimiters.
`inferGroupingRemovals` supplies a deletion witness to the exact token checker.
Retained tokens, literal spellings, comments and line terminators must match;
only removed grouping delimiters and their local horizontal whitespace may differ.
It rejects indirect-call wrapper deletion. This lexical check does not replace
compiler equivalence.

`tests/tools/grouping_bytecode.py --old OLD --new NEW --out SCRATCH` enumerates
complete records from manifests, checks set equality, invokes the token checker,
runs `node --check`, compiles both sides sequentially and compares complete
ark_disasm text. It uses `--module --merge-abc`, identical explicit record/source
names and extension, separately default optimization and `--opt-level=0`.
Default disassembly excludes debug tables. Only the informational
`# source binary:` comment is discarded; registers, branches, exceptions,
literals and module metadata are never normalized. Commands and results are
recorded per pair. The JavaScript-only AST adapter unquotes valid identifier
export aliases identically on both sides (the compiler cannot parse those
string-named aliases). ArkTS is not passed off as JavaScript.

Pinned tool SHA-256:

- es2abc: `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`
- ark_disasm: `a82e0c92a6726eb5c515f96c1731984e2a3938a6ce98dc540c7b5e7c08860676`

Baseline: `build/review/wtbase/b/abc_structure` at d4076bd2. Outputs and complete
machine-readable reports are under `build/review/paren/`. Both cohort runs use
`eval_cohort.py`; paired Immersive and MultiCommunityApplication use previous
flags. Results below are per optimization setting, not summed across settings.

| Set | Complete OLD/NEW | Bytecode pass/differ/unverified | Token and Node pass/unverified | Removed pairs (JS verified) |
|---|---:|---:|---:|---:|
| Previous flags | 526/526 | 526/0/0 | 526/0 | 4,670 |
| Language source | 576/576 | 567/0/9 | 563/13 | 5,158 |
| Paired samples | 80/80 | 80/0/0 | 80/0 | 1,177 |

No complete record was added/lost. No token or Node check failed. An additional
`node --check` run over **all** generated JavaScript (including the extra partial
file in each cohort) passed **527 / 564 / 80**, with no failures. The 13 ArkTS
outputs are outside the Acorn/Node grammar. Four compile identically with
`--extension=ts`; nine fail on both sides at quoted export aliases and remain
unverified: o15/14172, 15945, 17212, 18574, 25203, 29177, 31000, 32157, 35772.
They are not counted as compiler passes. Removed-pair totals exclude ArkTS.

| Set | `((` OLD → NEW | `({` OLD → NEW |
|---|---:|---:|
| Previous flags | 8,348 → 5,735 | 5,273 → 3,367 |
| Language source | 9,794 → 6,812 | 5,899 → 3,781 |
| Paired samples | 1,465 → 816 | 1,214 → 725 |

## Ten actual output examples

These are exact changed lines (some open multi-line constructs). Conservative
parentheses still visible in these examples are intentional slice boundaries.

1. `o0/13660`

```js
// OLD
Column.margin(({bottom: 12, left: 12, right: 12}));
// NEW
Column.margin({bottom: 12, left: 12, right: 12});
```

2. `o0/13660`

```js
// OLD
($value8.alignment = (deviceInfo.deviceType == "tablet" ? DialogAlignment.Center : DialogAlignment.Bottom));
// NEW
($value8.alignment = deviceInfo.deviceType == "tablet" ? DialogAlignment.Center : DialogAlignment.Bottom);
```

3. `o1/12874`

```js
// OLD
for (let $phi1 = "", $phi2 = 0; ($phi2 < $value7); (++$phi2), (($phi1 = $phi6))) {
// NEW
for (let $phi1 = "", $phi2 = 0; ($phi2 < $value7); ++$phi2, ($phi1 = $phi6)) {
```

4. `o0/6954`

```js
// OLD
Text.visibility(($local0.isShowLoading ? Visibility.None : Visibility.Visible));
// NEW
Text.visibility($local0.isShowLoading ? Visibility.None : Visibility.Visible);
```

5. `o1/12874`

```js
// OLD
((($value10 = [])));
// NEW
(($value10 = []));
```

6. `o0/13660`

```js
// OLD
(($value8 = ({builder: null, cancel: null, autoCancel: true, alignment: null, gridCount: 4, customStyle: null})).builder = ({["builder"]: () => {
// NEW
(($value8 = {builder: null, cancel: null, autoCancel: true, alignment: null, gridCount: 4, customStyle: null}).builder = ({["builder"]: () => {
```

7. `o2/24420`

```js
// OLD
this.toolbarList.push(({
// NEW
this.toolbarList.push({
```

8. `o18/60829`

```js
// OLD
$local4.postMessage(({type: "received message--", message: $phi1}));
// NEW
$local4.postMessage({type: "received message--", message: $phi1});
```

9. `o0/6954`

```js
// OLD
return new Index((void 0), ({}));
// NEW
return new Index((void 0), {});
```

10. `o10/7325`

```js
// OLD
this.__symbol.set(({id: 125832361, type: 40000, params: [], bundleName: "com.samples.choosecomponent", moduleName: "entry"}));
// NEW
this.__symbol.set({id: 125832361, type: 40000, params: [], bundleName: "com.samples.choosecomponent", moduleName: "entry"});
```

## Final test results

Full build passed. Fixed source-regression: **65/65**. Grouping tests: **4/4**,
including five production-printer forms executed by Node, negative token edits,
plan mutations and exact resource boundaries. Requested broad sweep: **102/103**;
only the documented pre-existing `family_source_cli_test` fails. Two integration
expectations were changed solely for removed parentheses (`progressbar_record`
and `assignment_arguments`); their behavior and mutation assertions remain.
Logs: `build/review/paren/{build-final.log,fixed-verified.log,grouping-final.log,
sweep-verified.log,node-all.json}`. Final outputs and manifest record entries
were rechecked against the compiler-accepted copies after the grammar guards;
no differences. No commit or push was made.

# Slice 1b — additional print contexts (2026-10-19)

The default policy now covers expression statements, function/arrow and named
class values, object member bases, declaration initializers, return/throw values,
if heads, loop-condition expression operands, for-in RHS (Expression), for-of RHS
(AssignmentExpression), and spread operands. Unknown contexts keep grouping.

`ExpressionOperand::statement_start` travels only down the actual printed
leftmost path: member/call base, binary/logical/sequence left, conditional test,
and member-assignment target. Emitting a group or a leading operator terminates
that path. Committed declaration/value/Boolean projections preserve the flag
when forwarding an operand. `ExpressionHeadFacts` decides whether the actual head
is object/function/class syntax; the final identifier spelling handles `let [`.
This keeps `({p: x}).p;`, while allowing `return {p: x}.p;`. There is no repeated
left-spine scan or projected-occurrence adapter. Only fixed-size action fields
were added; the existing prepaid action allocation accounts for their sizes.

Function opening/closing actions carry the same grouping decision. Inferred-name
object envelopes and `(0, function...)` remain unchanged. Named class expressions
have a typed bare expression form distinct from declarations and from wrapped
name-inference forms. Member/call bases remain grouped. Creation timing, names,
scopes and directives do not change.

No switch or template-substitution node exists in the current source-tree variant;
this slice does not invent either construct or post-process emitted strings.
The existing synthesized loop-header envelopes remain conservative where they
have no operand context. Primitive literal spellings such as `(void 0)` and
parenthesized negative/non-finite numbers are unchanged. For-init `[In]`, optional-chain boundaries, template
tags, directive strings, indirect calls and all blocks are unchanged. Reserved
`let` bindings are still rejected by the tree's binding validator; the lexical
lookahead rule is tested independently, and the typed tree refusal is retained.

## Slice 1b acceptance

The requested `git -C build/review/wtbase checkout ...` was denied by the sandbox
at `.git/worktrees/wtbase/index.lock`. Before editing, the clean main checkout at
**b24c0a922f7c3975cf95c7643499cb0d858e8599** was built and its executable retained
as `build/review/paren1b/base/abc_structure`. Its build log and SHA-256 are beside
it. This supplies the same committed OLD source, without modifying Git metadata.

All cohort outputs, tool commands and reports are under `build/review/paren1b/`.
The same pinned compiler/disassembler and adaptations described above were used.
The following compiler counts apply independently to **default optimization and
`--opt-level=0`**; no instruction/register/branch/handler/metadata normalization.

| Set | Complete OLD/NEW | Bytecode pass/differ/unverified | Token pass/unverified | Removed pairs (JS) |
|---|---:|---:|---:|---:|
| Previous flags | 526/526 | 526/0/0 | 526/0 | 11,399 |
| Language source | 576/576 | 567/0/9 | 563/13 | 12,928 |
| Paired samples | 80/80 | 80/0/0 | 80/0 | 1,301 |

The same nine ArkTS quoted-export failures listed in slice 1a remain unverified
on both sides. Token checks have zero failures. All generated JavaScript passes
`node --check`: **527 / 564 / 80**, including the extra partial file in each
cohort. Final rebuilt outputs and all manifest record entries match the
compiler-checked copies byte-for-byte (`final-recheck.json`).

| Set | `((` OLD → NEW | `({` OLD → NEW |
|---|---:|---:|
| Previous flags | 5,735 → 4,178 | 3,367 → 2,949 |
| Language source | 6,812 → 5,004 | 3,781 → 3,324 |
| Paired samples | 816 → 585 | 725 → 652 |

New family tests execute 32 function/arrow/async/context combinations and check
inferred names; printer tests check statement-head versus value object members.
Existing verifier mutation and exact 200/201/400/800 width/depth budgets remain.
All textual test changes retarget the same mutations after grouping removal;
the callback mutation now uses the existing AST editor. No behavior comparison
or mutation detection assertion is weakened.

## Ten slice 1b output examples

1. `o1/12874`

```js
// OLD
let calc = (function(inputContent) {
// NEW
let calc = function(inputContent) {
```

2. `o0/3730`

```js
// OLD
windowStage.loadContent("pages/Index", ((err, data) => {
// NEW
windowStage.loadContent("pages/Index", (err, data) => {
```

3. `o0/13660`

```js
// OLD
return ({cancel: $local9.onCancel, confirm: $local9.onAccept, deviceName: $local9.deviceInfo.deviceName, deviceInfo: $local9.__deviceInfo});
// NEW
return {cancel: $local9.onCancel, confirm: $local9.onAccept, deviceName: $local9.deviceInfo.deviceName, deviceInfo: $local9.__deviceInfo};
```

4. `o0/13660`

```js
// OLD
(this.cancel = (() => {
// NEW
this.cancel = () => {
```

5. `o0/13660`

```js
// OLD
return ({id: 16777261, type: 20000, params: [], bundleName: "com.samples.devicemanager", moduleName: "entry"});
// NEW
return {id: 16777261, type: 20000, params: [], bundleName: "com.samples.devicemanager", moduleName: "entry"};
```

6. `o15/44200`

```js
// OLD
const $value0 = (class Log {
// NEW
const $value0 = class Log {
```

7. `o0/13660`

```js
// OLD
($local9 = this);
// NEW
$local9 = this;
```

8. `o0/13660`

```js
// OLD
(($value8 = {builder: null, cancel: null, autoCancel: true, alignment: null, gridCount: 4, customStyle: null}).builder = ({["builder"]: () => {
// NEW
($value8 = {builder: null, cancel: null, autoCancel: true, alignment: null, gridCount: 4, customStyle: null}).builder = ({["builder"]: () => {
```

9. `o0/13660`

```js
// OLD
}), Column);
// NEW
}, Column);
```

10. `o0/6954`

```js
// OLD
return ({deviceInfo: item, visible: Visibility.Visible});
// NEW
return {deviceInfo: item, visible: Visibility.Visible};
```

Final tests: full build passed; fixed source regression **65/65**; grouping,
family and function-name targets **9/9**; updated integration tests **16/16**.
The requested CLI/corpus/failure/receipt/manifest/position/grouping/call-spread/
staged sweep passes **96/97**; only the documented pre-existing
`family_source_cli_test` owner/origin-arena assertion fails. Text-sensitive
mutation selectors now match bare assignments/arrows, and the partial-family
ASI assertion searches the function keyword without requiring an unnecessary
opening parenthesis. Mutation non-equality and behavior assertions remain.
Logs: `build/review/paren1b/{full-build-final,fixed-final,sweep-final,grouping-tests-final,updated-tests}.log`.
