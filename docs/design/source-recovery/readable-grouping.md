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
