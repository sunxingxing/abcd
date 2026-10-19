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

# Slice 2 — identifier value calls (2026-10-19)

`PlainValueCallee` removes the synthetic comma wrapper only at `CallExpr` with
`CallReference::kValue`, for a strict validated tree and a final `SymbolExpr` or
`GlobalNameExpr` spelling other than `eval`. Symbol spelling uses
`ReadBinding` then `Storage`, exactly as printing does. Literal transports,
replacement expressions and literal read projections conservatively retain the
wrapper. The validated scope kinds (parameters/body/block/captures/module/self/
catch) contain no object/with environment; strictness alone is not a claim about
an arbitrary function created inside a foreign with environment. This rule is
for these verified source trees, not arbitrary JavaScript input.

The rule does not run for construction, optional-call projections, tags, member
callees, inferred-name suppression or typeof-global value projections. Sloppy
and unproven cases retain the wrapper. Existing admission rejects reserved
strict binding names such as a local/import alias `eval`; that refusal remains.
The final-spelling exclusion also protects any admissible global eval reference.

ECMA-262 [EvaluateCall](https://tc39.es/ecma262/multipage/ecmascript-language-expressions.html#sec-evaluatecall)
uses GetThisValue for property references and WithBaseObject for environment
references. [Declarative](https://tc39.es/ecma262/multipage/executable-code-and-execution-contexts.html#sec-declarative-environment-records-withbaseobject)
and [global](https://tc39.es/ecma262/multipage/executable-code-and-execution-contexts.html#sec-global-environment-records-withbaseobject)
environments supply undefined; module environments are declarative. The call
expression's direct-eval special case is why the spelling `eval` is excluded.
The pinned `references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp`
sets `callThis=false` in CALLARG0 at 1268–1273 and `callThis=true` in CALLTHIS0 at
1347–1353; line 1512 passes VALUE_UNDEFINED for the former, versus the explicit
receiver for the latter. No call opcode or receiver proof changes in this slice.

## Slice 2 acceptance policy

`grouping_bytecode.py --value-calls` is opt-in; its default remains strict.
The Acorn checker accepts only removal of a call-callee `(0, Identifier)` with
non-eval spelling, excluding optional/new/tag and other value contexts. All
other tokens, comments and line terminators must remain. The bytecode allowance
is deletion of `ldai 0x0` immediately before an identical accumulator-overwriting
register or lexical/module/global binding load; no intervening label, store or
accumulator-consuming instruction. The runtime load handlers are at 2792/2834,
6067/6084, 6304/6323 in interpreter-inl.cpp. The matching instruction and every
other assembly line (including labels, handlers, registers and metadata) must
be identical. Deleted loads are counted and bounded by source wrapper witnesses.
ArkTS without a token witness is unverified, never an allowed-difference pass.

OLD is the supplied `build/review/wtbase/b/abc_structure` at **1d62926c**.
Final compiler reports are `build/review/paren2/check-previous/report.json`,
`build/review/paren2/accept-source/report.json`, and
`build/review/paren2/accept-paired/report.json`; each records compiler identity
and commands. Earlier probe/check-source reports are not acceptance results.

| Set | Complete | Default pass/allowed/differ/unverified | O0 pass/allowed/differ/unverified | Token pass/unverified | Verified removed wrappers |
|---|---:|---:|---:|---:|---:|
| Previous flags | 526 | 526/0/0/0 | 284/242/0/0 | 526/0 | 704 |
| Language source | 576 | 567/0/0/9 | 296/270/0/10 | 563/13 | 979 |
| Paired samples | 80 | 80/0/0/0 | 69/11/0/0 | 80/0 | 21 |

All complete sets are unchanged. The nine pre-existing ArkTS compilation failures
remain; O0 additionally leaves o15/45013 unverified because its wrapper deletion
has no JS-grammar witness. Node syntax passes all 526/563/80 complete JavaScript
outputs. Textual `(0, ` counts are **1260→556**, **1613→632**, **115→94**;
the source total includes two removals in ArkTS, excluded from the verified
wrapper count. Counting and real sinks use the same decision and original
position-map machinery; no new allocation or subtree walk is introduced.

Ten cohort examples (unchanged surrounding source omitted):

`o0/13660`
```js
// OLD
      Image.create($local0.getDeviceTypeIcon((0, Number)($local0.deviceInfo.deviceType)));
// NEW
      Image.create($local0.getDeviceTypeIcon(Number($local0.deviceInfo.deviceType)));
```

`o0/6954`
```js
// OLD
        (0, setTimeout)(() => {
// NEW
        setTimeout(() => {
```

`o0/6954`
```js
// OLD
          (0, itemCreation)(elmtId, isInitialRender);
// NEW
          itemCreation(elmtId, isInitialRender);
```

`o0/6954`
```js
// OLD
(0, registerNamedRoute)(() => {
// NEW
registerNamedRoute(() => {
```

`o1/12874`
```js
// OLD
  const $value1 = (0, parseInfixExpression)(inputContent);
// NEW
  const $value1 = parseInfixExpression(inputContent);
```

`o1/12874`
```js
// OLD
  const $value2 = (0, toSuffixExpression)($value1);
// NEW
  const $value2 = toSuffixExpression($value1);
```

`o1/12874`
```js
// OLD
  return (0, calcSuffixExpression)($value2);
// NEW
  return calcSuffixExpression($value2);
```

`o1/12874`
```js
// OLD
    if (!(0, isOperator)($value2)) {
// NEW
    if (!isOperator($value2)) {
```

`o1/12874`
```js
// OLD
    if (!(0, isGrouping)($value12)) {
// NEW
    if (!isGrouping($value12)) {
```

`o1/12874`
```js
// OLD
        if (!(0, isSymbol)($value12) || !($phi2 === 0 || inputContent[$phi2 - 1] === "(")) {
// NEW
        if (!isSymbol($value12) || !($phi2 === 0 || inputContent[$phi2 - 1] === "(")) {
```

Final verification: full build passed; `source-regression -j 1` **65/65**;
grouping tests **7/7** (including the narrow bytecode-allowance mutations,
callee reassignment and indirect-eval alias differential). The required sweep
passes **98/99**, only the documented pre-existing `family_source_cli_test`
owner/origin-arena assertion. A broader receiver/reference run passes **15/29**;
the 14 failures are the registered derived/captured/forwarded receiver variants
listed in status.md's existing-failure inventory, failing family construction
before printing. Native function-name and call-reference tests pass.
Text-sensitive mutation selectors were updated to bare call spellings; their
non-equality and behavioral assertions remain in force.

The final executable regenerates byte-identical text to the compiler-checked
copies (`final-output-check.json`). Node syntax checks including partial outputs
pass **527 / 564 / 80**, zero failures (`node-all.json`). `git diff --check`
passes. Final logs: `build/review/paren2/{build-final,fixed-final,sweep-final,grouping-final,targeted-final}.log`.
No commit, push or stash.


## Slice 3: scope-safe braces (2026-10-19)

Baseline: `edc9821e`, using `build/review/wtbase/b/abc_structure`.
`BracePlan` is computed once per tree in `PrepareSourceVariables`, after binding,
nullish and loop projections. It records a typed reason for every statement.
The printer's statement/block cursor carries the resulting braced/list/else-if
form; the counting sink and real sink execute the same actions. No source tree,
control edge, binding, name, expression grouping or value-call rule is changed.

The prepaid scope census rejects any binding-bearing scope, the capture scope,
non-block scope kinds and explicit binding/loop/try origins. Labels and targeted
blocks are retained. Declarations (including relocated lexical initializers and function/class initializers),
unrecovered text, relocated/projected content and empty blocks are retained.
Only raw single-if alternates qualify for `else if`; extra omitted statements
are conservatively not treated as sole printed content. The tree verifier
requires the inner if's arms to be blocks, and this plan keeps those arms.
Thus removing the alternate's wrapper cannot reassign a dangling else.

Bare blocks qualify only as children of a statement list. Their empty lexical
scope has no binding/environment duty. An unlabelled BlockEvaluation returns
its StatementList completion unchanged: normal/abrupt kind, value and target,
including UpdateEmpty, remain in order. Blocks beginning with a string literal
are retained everywhere, a conservative superset of the directive-position
restriction. Nested retained blocks cannot expose a directive. Function,
try/catch/finally, if and loop bodies remain grammar-required. Switch and static
bodies are not candidates in this tree representation. All original child spans
and textless block anchors are recorded by the emitted token stream; no offsets
are patched afterwards.

Planning uses O(statements + children + scopes + symbols) work and storage, with
no subtree rescans or recursion. All vectors are admitted before allocation;
temporary census storage is released from the ledger only after destruction.
`VerifyBracePlan` independently recomputes decisions from the immutable tree
and projections. Unit tests mutate the decision, retain binding/capture/catch/
label/directive bodies, check dangling else and child/anchor spans, and exercise
200/201/400/800 width and depth with exact byte/work boundaries. The Node test
checks engine-printed syntax and seven independent completion/else-pairing cases.

### Acceptance

`grouping_bytecode.py --braces` uses strict assembly comparison: no dead-load
allowance, register renaming or branch/handler normalization. The real Acorn
lexer allows only matching eligible block braces to disappear; AST comparison
checks statement order and if/else pairing. Literal spelling, comments and all
other tokens remain intact. Export-name adaptation is identical on both sides.

| Input | Complete OLD/NEW | Else-if | Bare flatten | Strict default pass/differ/unverified | Strict O0 pass/differ/unverified | Token pass/fail/unverified | Node pass/fail/unverified |
|---|---:|---:|---:|---|---|---|---|
| previous flags, 40 inputs | 526/526 | 96 | 0 | 526/0/0 | 526/0/0 | 526/0/0 | 526/0/0 |
| language-source, 40 inputs | 576/576 | 106 | 0 | 567/0/9 | 567/0/9 | 563/0/13 | 563/0/13 |
| two paired samples | 80/80 | 0 | 0 | 80/0/0 | 80/0/0 | 80/0/0 | 80/0/0 |

Complete record sets are identical. The 13 ArkTS files are outside Acorn/Node;
all are text-identical in this slice. Nine have the same pre-existing es2abc
compilation failure on both sides, not a pass. Four compile identically.
No cohort bare block met the conservative scope criteria; constructed trees
exercise flattening. This is an explicit coverage boundary, not a claim that
all visual nested braces are redundant.

Compiler SHA-256: `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
Disassembler SHA-256: `a82e0c92a6726eb5c515f96c1731984e2a3938a6ce98dc540c7b5e7c08860676`.
Commands, bytecode, token decisions and reports are under
`build/review/braces/{finalcheck,finalchecks,checkpair}`; per-site counts are in
`{check,checks,checkpair}/brace-counts.json`.

Ten representative rewrites (arm contents abbreviated):

`o1/12874`
```js
// OLD
} else {
  if ($value4.length) {
    // ...
  }
}
// NEW
} else if ($value4.length) {
  // ...
}
```

`o14/24464`
```js
// OLD
} else {
  if (res != 0) {
    // ...
  }
}
// NEW
} else if (res != 0) {
  // ...
}
```

`o14/26141`
```js
// OLD
} else {
  if ($value0 === "fun1") {
    // ...
  }
}
// NEW
} else if ($value0 === "fun1") {
  // ...
}
```

`o15/7013`
```js
// OLD
} else {
  if (!islandSpace) {
    // ...
  }
}
// NEW
} else if (!islandSpace) {
  // ...
}
```

`o16/19142`
```js
// OLD
} else {
  if ($value2 === audio.AudioStreamDeviceChangeReason.REASON_NEW_DEVICE_AVAILABLE) {
    // ...
  }
}
// NEW
} else if ($value2 === audio.AudioStreamDeviceChangeReason.REASON_NEW_DEVICE_AVAILABLE) {
  // ...
}
```

`o17/4729`
```js
// OLD
} else {
  if (state === "initialized") {
    // ...
  }
}
// NEW
} else if (state === "initialized") {
  // ...
}
```

`o18/19448`
```js
// OLD
} else {
  if (!$value1.caContent) {
    // ...
  }
}
// NEW
} else if (!$value1.caContent) {
  // ...
}
```

`o18/54057`
```js
// OLD
} else {
  if ($value1 === "connectServer") {
    // ...
  }
}
// NEW
} else if ($value1 === "connectServer") {
  // ...
}
```

`o18/59564`
```js
// OLD
} else {
  if ($value1 === "disConnect") {
    // ...
  }
}
// NEW
} else if ($value1 === "disConnect") {
  // ...
}
```

`o18/60829`
```js
// OLD
} else {
  if ($value1 === "sendMessage") {
    // ...
  }
}
// NEW
} else if ($value1 === "sendMessage") {
  // ...
}
```

A fixed-suite regression caught an important eligibility boundary during this
slice: `BindingInitializers::statements` may introduce a lexical declaration in
an originally empty scope. The plan now rejects those projected declarations,
not just original `DeclarationStmt`/scope symbols. Constructed form 30 of
`binding_interference_source_test` exposed this; its semantic oracle was kept
unchanged, and `brace_plan_test` has a focused regression for the projection.
The corrected executable regenerates byte-identical source to the complete
compiler-checked cohort/paired outputs (`final-output-check.json`); no compiler
comparison was waived or normalized. Node checks of all new JS outputs,
including partial files, pass **527 / 564 / 80**, zero failures.

Final verification: full build passed; `ctest --preset source-regression -j 1`
**65/65**; brace/grouping **9/9**; required sweep **98/99**, only the documented
pre-existing `family_source_cli_test` owner/origin-arena failure. The corrected
binding differential passes unchanged (including its source-fixture prerequisite).
Logs: `build/review/braces/{build-corrected,regression-final,focused-final,sweep-final,binding-corrected}.log`.
`git diff --check` passes. No commit, push or stash.


## Slice 1c: residual value contexts (2026-10-19)

Baseline `6bdd4ae9`. The census parses every emitted JS/ArkTS source file,
including incomplete files, with the pinned SDK TypeScript parser. String-named
export aliases are replaced by same-width placeholder identifiers only in the
parser's input: original source offsets and examples are retained. No source
output is adapted by this tool. All files parse successfully.
`tests/tools/grouping_census.mjs` streams occurrence queries to the test-only
`grouping_census_probe`, which invokes production `ReadableGrouping` and
`ReadableLiteralEnvelope` with typed syntax/context values. An unmapped AST
context or child remains `kUnknown`. This is a printed-grammar census, not a
claim to reconstruct recovery provenance; `kNone` means the mapped grammar
permits removal, not that an unknown synthetic wrapper's other duties may be
ignored. Intrinsic wrapper detail is recorded separately from that decision.
The complete occurrence lists, parser hash and both top-15 reports are in
`build/review/paren1c/census-{before,after}.{json,txt}`; previous-flags censuses
are in `census-oldflags-{before,after}.{json,txt}`.

The constructor argument already had AssignmentValue context. Its extra group
came from the unconditional undefined literal spelling, not the argument cursor.
`ReadableLiteralEnvelope` now permits bare `void 0` only in explicit Expression
or AssignmentValue contexts. Default parameter cursors now supply AssignmentValue
as well. Operator/postfix/unknown contexts and signed/nonfinite numeric literal
spelling remain conservative. No precedence of a synthetic unary spelling is
mistaken for that of a primary literal.

The paramsGenerator example is a synthesized name-preserving member selection:
`({["paramsLambda"]: () => { ... }})["paramsLambda"]`. The computed key,
function creation and selection remain intact. Its object envelope is omitted
only where the existing function operand decision is ungrouped and there is no
self-binding IIFE. Statement/call/member/constructor-sensitive and unknown uses
retain the envelope. `(0, ...)`, inferred-name suppression, classes and all
braces are unchanged. No new allocations or analysis walks enter production.

### Census and scope

Source cohort, all files: **5,861 -> 3,985** grouping pairs (**1,876 removed**).
Previous flags: **4,991 -> 3,354** (**1,637 removed**). These are AST grouping
pairs, not call syntax, parameter lists or control-head delimiters. Complete-JS
token-verified removals are **1,624 / 1,786 / 220** for previous/source/paired;
the all-file source difference additionally includes 77 ArkTS pairs and 13
pairs in the partial JS output.

Top 15 before:

| Count | Parent context | Child | Decision | Example |
|---:|---|---|---|---|
| 1181 | Binary/ExclamationEqualsEqualsToken/right | VoidExpression | kUnknown | (void 0) |
| 631 | MemberBase | ObjectLiteralExpression | kNone | ({["paramsLambda"]: () => { return {}; }}) |
| 526 | AssignmentValue/NewArgument | VoidExpression | kNone | (void 0) |
| 415 | Unknown/ExpressionWithTypeArguments | Identifier | kUnknown | (ViewPU) |
| 411 | Unary/PrefixUnaryExpression | BinaryExpression | kPrecedence | ("finalizeConstruction" in ViewPU.prototype) |
| 313 | AssignmentValue/Parameter | PrefixUnaryExpression | kUnknown | (-1) |
| 296 | AssignmentValue/Parameter | VoidExpression | kNone | (void 0) |
| 236 | StatementStart | AwaitExpression | kNone | (await store.setDistributedTables(["EMPLOYEE", "EMPLOYEE2"], relationalStore.DistributedType.DISTRIBUTED_DEVIC |
| 233 | Expression/ReturnStatement | VoidExpression | kNone | (void 0) |
| 201 | Binary/PlusToken/right | BinaryExpression | kPrecedence | (this.textInfo + "\u000a") |
| 188 | MemberBase | BinaryExpression | kPrecedence | ($value0 = $local1.uiContext) |
| 158 | CallCallee | BinaryExpression | kPrecedence | (0, ({["observedDeepRender"]: () => { $local2.observeComponentCreation2($local1, GridItem); $local2.observeCom |
| 116 | MemberBase | ConditionalExpression | kPrecedence | ($local4 ? $local4 : $local5) |
| 104 | AssignmentValue/VariableDeclaration | VoidExpression | kNone | (void 0) |
| 93 | Unknown/ForStatement | BinaryExpression | kUnknown | ($phi0 < $value7.length) |

Top 15 after:

| Count | Parent context | Child | Decision | Example |
|---:|---|---|---|---|
| 1181 | Binary/ExclamationEqualsEqualsToken/right | VoidExpression | kUnknown | (void 0) |
| 415 | Unknown/ExpressionWithTypeArguments | Identifier | kUnknown | (ViewPU) |
| 411 | Unary/PrefixUnaryExpression | BinaryExpression | kPrecedence | ("finalizeConstruction" in ViewPU.prototype) |
| 313 | AssignmentValue/Parameter | PrefixUnaryExpression | kUnknown | (-1) |
| 236 | StatementStart | AwaitExpression | kNone | (await store.setDistributedTables(["EMPLOYEE", "EMPLOYEE2"], relationalStore.DistributedType.DISTRIBUTED_DEVIC |
| 201 | Binary/PlusToken/right | BinaryExpression | kPrecedence | (this.textInfo + "\u000a") |
| 188 | MemberBase | BinaryExpression | kPrecedence | ($value0 = $local1.uiContext) |
| 158 | CallCallee | BinaryExpression | kPrecedence | (0, ({["observedDeepRender"]: () => { $local2.observeComponentCreation2($local1, GridItem); $local2.observeCom |
| 116 | MemberBase | ConditionalExpression | kPrecedence | ($local4 ? $local4 : $local5) |
| 93 | Unknown/ForStatement | BinaryExpression | kUnknown | ($phi0 < $value7.length) |
| 78 | Binary/EqualsEqualsEqualsToken/right | VoidExpression | kUnknown | (void 0) |
| 56 | Unknown/ForStatement | PrefixUnaryExpression | kUnknown | (++$phi0) |
| 50 | AssignmentValue/VariableDeclaration | AwaitExpression | kNone | (await store.sync(relationalStore.SyncMode.SYNC_MODE_PUSH, $value6)) |
| 37 | MemberBase | ObjectLiteralExpression | kNone | ({["observedDeepRender"]: () => { $local2.observeComponentCreation2($local1, GridItem); $local2.observeCompone |
| 25 | StatementStart | ParenthesizedExpression | kUnknown | (($phi3 = 0)) |

The required assignment-as-member-base group remains: all **188** such
source-cohort occurrences are unchanged. Remaining `kNone` await and synthetic
wrapper entries are census findings, not broadened admission in this slice.
Await/yield/delete envelopes, heritage, loop headers, signed numbers and
operator-position undefined literals remain outside this bounded change.

### Acceptance

| Input | Complete OLD/NEW | Strict default pass/differ/unverified | Strict O0 | Token pass/fail/unverified |
|---|---:|---|---|---|
| previous flags | 526/526 | 526/0/0 | 526/0/0 | 526/0/0 |
| language-source | 576/576 | 567/0/9 | 567/0/9 | 563/0/13 |
| paired samples | 80/80 | 80/0/0 | 80/0/0 | 80/0/0 |

No allowed bytecode differences or normalization were used. Nine files fail
compilation on both sides for the existing reasons; 13 ArkTS files are outside
Acorn/Node. All-JS Node syntax checks, including partial outputs, pass
**527 / 564 / 80**, with zero failures. Compiler identity/options/diffs and
individual records are in `build/review/paren1c/{check,checks,checkpair}`.
Tests cover constructor arguments, required member-base grouping, conservative
unknowns, name inference, values, counting/real sink spans, and 200/201/400/800
width/depth/budget cases. Two selectors changed solely to follow new spellings:
ProgressBar's fabricated-default mutant and the native-name wrapper-presence
assertion. Their behavioral/name/mutation assertions are unchanged.

Ten changed-line examples (enclosing bodies are omitted):

`o14/18164`
```js
// OLD
return new TaskpoolCancel((void 0), {});
// NEW
return new TaskpoolCancel(void 0, {});
```

`o0/13660`
```js
// OLD
$value0.paramsGenerator_ = ({["paramsLambda"]: () => {
// NEW
$value0.paramsGenerator_ = {["paramsLambda"]: () => {
```

`o0/6954`
```js
// OLD
$value2.paramsGenerator_ = ({["paramsLambda"]: () => {
// NEW
$value2.paramsGenerator_ = {["paramsLambda"]: () => {
```

`o0/3730`
```js
// OLD
return (void 0);
// NEW
return void 0;
```

`o0/13660`
```js
// OLD
constructor(parent, params, __localStorage, elmtId = (-1), paramsLambda = (void 0), extraInfo) {
// NEW
constructor(parent, params, __localStorage, elmtId = (-1), paramsLambda = void 0, extraInfo) {
```

`o0/13660`
```js
// OLD
$local9.__deviceInfo = new ObservedPropertyObjectPU((void 0), $local9, "deviceInfo");
// NEW
$local9.__deviceInfo = new ObservedPropertyObjectPU(void 0, $local9, "deviceInfo");
```

`o0/13660`
```js
// OLD
($value0 = new DeleteDeviceDialog($local9, {cancel: $local9.onCancel, confirm: $local9.onAccept, deviceName: $local9.deviceInfo.deviceName, deviceInfo: $local9.__deviceInfo}, (void 0), -1, () => {
// NEW
($value0 = new DeleteDeviceDialog($local9, {cancel: $local9.onCancel, confirm: $local9.onAccept, deviceName: $local9.deviceInfo.deviceName, deviceInfo: $local9.__deviceInfo}, void 0, -1, () => {
```

`o0/6954`
```js
// OLD
const $value2 = ({["forEachItemGenFunction"]: (_item, index) => {
// NEW
const $value2 = {["forEachItemGenFunction"]: (_item, index) => {
```

`o0/13660`
```js
// OLD
let $local9 = (void 0);
// NEW
let $local9 = void 0;
```

`o1/12874`
```js
// OLD
return new Index((void 0), {}, $local19.useSharedStorage ? LocalStorage.getShared() : (void 0));
// NEW
return new Index(void 0, {}, $local19.useSharedStorage ? LocalStorage.getShared() : void 0);
```

Final verification: full build passed; fixed suite **65/65**; grouping/brace
**10/10**; required sweep **101/102**, only the documented pre-existing
`family_source_cli_test` failure. Logs are
`build/review/paren1c/{build-final2,fixed-final,focused,sweep-final}.log`.
On the JS-only population matching the review's baseline counts:
`(` **56,512 -> 54,713**, `((` **4,928 -> 4,744**, `({` **3,246 -> 2,685**,
`(0, ` **623 -> 623**. All-file counts (including ArkTS) are recorded separately
in `text-counts.json`. No commit, push or stash.
