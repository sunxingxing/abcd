# Native switch and fall-through correspondence

2026-10-20; baseline `f5c62475`. No new source-language premise.

## Result and original-edge evidence

`o18/57687`, `m58255 handleMessage`, completes under `--language-source`.
Previous flags still stop earlier at `m57863 PC123`, capture depth 0 / slot 8
(`CaptureCellError(3) > CaptureError(1) > ScopeError(3)`), unchanged.

The original discriminant is `v26` from op7 PC14 (`message.type`). Tests
op10/13/16/19 compare that same value strictly against the four case strings;
branches op11/14/17/20 take matching edges e0/e2/e4/e6. The send case is B21:
e31 enters B23 directly and e32 enters B22, whose e33 enters B23 after the send.
B23 PC297 contains the actual close-case reads/call. It now has exactly one
case owner; e31/e33 are explicit fall-through ports, not copied effects.

## Proof and source representation

`switch_region.{h,cc}` is entered only after the ordinary builder's duplicate
ownership census and epsilon forwarding both fail. Existing successful plans
are returned untouched. Case discovery uses canonical SSA identity, normal
and post-dominance, plus a shared edge-split dominator index: each later test
must be entered under the preceding miss edge, so consecutive independent
switches cannot accidentally become one chain. No PC or block-number ordering
is used. Default position follows actual fall-through constraints; only empty
adjacent labels may share one body's entry. Every original edge is retained in
the projection and rechecked against fresh graph-derived membership/ports.
A non-adjacent shared body remains a typed `kCrossEntry` refusal.

`SwitchStmt` owns its block and `SwitchCaseStmt` children. Each clause retains
its canonical original strict-comparison ExprId (including a Boolean complement
of strict inequality). `switch_syntax` independently proves that factoring the
repeated left operand is a read of one noncaptured private cache/parameter,
with at most its first initializing assignment and no test-side write. The
printer renders that first operand once in the header and each right operand
as its case selector. No expression identity is fabricated or borrowed from a
later case. Empty case labels have REQUIRED retention; recovery refuses a
projection that drops them. Case/test provenance uses the actual token stream;
counting and real sinks use the same actions and omission boundaries.

TreeFlow independently checks each case's test/body order, target, break and
fall-through original edge, and empty edge-copy programs. OriginalFlow retains
the original branch/operation identities. RecoveryFlow rebuilds lazy case
selection and case-body fall-through, using the original comparison ExprIds;
all normal/throw event correspondence is checked by the existing joint recovery
verifier on every print projection. Native breaks have explicit switch targets
and labels; loop continue retains its original LoopId/transfer proof. Nearest
breakable-statement tracking is separate from nearest-loop tracking, so a
loop break crossing a switch requires its original loop label. Cell
lifetimes traverse the same dispatch/case view through typed switch/case scopes,
never mapping unvisited original blocks to function scope.

The semantics reference is [ECMA-262 CaseBlockEvaluation and CaseClauseIsSelected](https://tc39.es/ecma262/multipage/ecmascript-language-statements-and-declarations.html#sec-runtime-semantics-caseblockevaluation): selectors are tested in order using strict equality; bodies after a match
fall through without further tests. Middle-default selection is distinct from
its body position. The pinned dynamic frontend corroborates the lowering in
`references/openharmony/arkcompiler/ets_frontend/es2panda/compiler/core/switchBuilder.cpp:43-66`
and `ir/statements/switchStatement.cpp:39-73`: discriminant save once, ordered
tests/default jump, then case bodies in order without invented breaks.

This remains a normal-control view. Branching case-expression regions, phased
exceptional control, captured/shared lexical scopes without a verified placement,
non-adjacent shared effects, nonempty unrepresented phi copies and unsupported
loop continuation corridors keep their typed refusals. In particular a for-loop
continue sharing an effectful increment epilogue needs a separate continuation
composition; the admitted loop fixture uses the verified direct loop-header
continue. No successful prior if-ladder is converted as a shortcut.

## Verification

Scratch evidence is under `build/review/native-switch/`. Final reruns use the
`*-validated.log` / `validated-{previous,source}/` paths.

- Full build: `full-build-validated.log`.
- Source regression: **65/65** (`source-regression-validated.log`).
- New tests: **3/3** (`targeted-validated.log`). The fixture covers two/three-case
  fall-through, empty labels, default in the middle/at the end, a scoped let,
  nested-if break, return, loop continue, and lazy effectful case expressions.
- Node differential: **3,150 scenarios per contract** on real es2abc output;
  case/order mutations are observed. The target callback separately matches
  **260 paired-source scenarios**, including throwing getters/calls and a
  changing discriminant; inserting a break at the send-to-close fall-through
  is observed. Evaluation source never enters the decompiler.
- Control/TreeFlow mutation tests reject altered discriminant/ports, reordered
  cases, missing tests and a non-adjacent shared entry. Copy/register and split
  corridor variants pass. Both sequential depth and clause width are tested at
  200/201/400/800, with exact byte/work admission and one-less refusal. At 800
  sequential switches: control 14,871,421 bytes / 4,675,960 work; TreeFlow admits
  exactly 15,748,663 bytes / 2,141,730 work (its retained/check counter reports
  12,699,556 bytes). At width 800: control 5,987,628 bytes /
  1,907,264 work; TreeFlow admits exactly 6,368,444 bytes / 859,216 work.
  See `scale-validated.log` for all boundaries. The legacy work-only
  control verifier API does not return its transient memory peak, so TreeFlow's
  admitted byte boundary is measured independently rather than inferred from
  its retained-output counter.
- Sweep: **186/192 pass**, with only the six pre-existing failures:
  `family_source_cli_test`, `tree_structure_test`, `try_tree_test`,
  `class_tree_source_test`, `try_tree_source_test`,
  `file_generator_return_loop_source_test`. All six were rebuilt/reproduced
  against wtbase `f5c62475` (`baseline-failures.json`, individual logs).
- Cohorts: previous flags **538/620 -> 538/620**; source **593/620 -> 594/620**.
  Both 40-input runs have zero process failures, regressions, or changes to
  previously complete source bytes. Previous manifests are identical; the only
  source-contract manifest record that changes is o18/57687. Metadata-only
  records account for the helper scripts' 1007-row denominator.
- Peak RSS: four-worker child peak upper bounds 148,946,944 / 154,681,344 bytes
  (previous/source), plus parent peaks 18,644,992 / 18,677,760 bytes; below 1GB.
  `node --check` passes on the newly published record and fixture outputs.

An unrestricted TEST-ONLY probe was separately built in scratch; production
selection was not changed. An Acorn AST census of its verified complete output
finds **41 previously complete records / 68 native sites** under previous flags,
and **51 records / 89 sites** under source. Twenty other formerly complete
records hit typed native-projection gates in that counterfactual. None of these
conversions or diagnostic changes is present in production cohort output.

## Compiler round trip (not a byte-identical claim)

Fixture compiler identity/options/hashes are checked into
`tests/integration/fixtures/switch_fallthrough/compiler.json`.
`tests/tools/switch_roundtrip.mjs` preserves each original/emitted signature and
body verbatim and puts BOTH in the same per-method module/export wrapper. It
feeds `tests/tools/grouping_bytecode.py`; both sides receive the identical
identifier-valued string-export adaptation. No registers, branches, handlers,
operands or metadata are normalized. The grouping-only TOKEN comparison is
intentionally not an acceptance oracle for this structural recovery change.

All seven method pairs recompile/disassemble at default and O0. Strict assembly
comparison: **default 4 identical / 3 different; O0 0 identical / 7 different**.
There are no uncompiled/unverified pairs. Raw differences are retained in
`method-bytecode/*/{default,O0}.diff`, not relabelled passes:

| Method | Exact remaining difference and semantic explanation |
|---|---|
| `three`, `middle`, `empty` | Identical at default. O0 has an explicit jump to the immediately following switch-follow label for the final native break. No call, test or exit is removed. |
| `ordered` | Identical at default. O0 additionally initializes/saves the private discriminant cache, shifts private registers, and adds the last break's jump-to-follow. The ordered selector calls and strict tests, default/body order, receivers and exceptions are unchanged. |
| `dispatch` | Private cache initialization/register transport at O0; existing inverted-if syntax materializes a Boolean before its branch (also visible at default). This shifts labels and IC indices/SlotNumber. Each getter/call remains once, in the same branch/order, with the same receiver. |
| `exits` | Existing inverted-if Boolean materialization, consequent/else jump layout, and shifted IC slots/labels; the break and return destinations/payloads are unchanged. |
| `loop` | Private phi/cache declarations and register transports at O0, native continue/break jumps; the discarded postfix numeric increment becomes prefix increment (no retained old result), removing redundant ToNumeric transport and shifting IC slots. Its accumulator is initialized numeric and stays numeric. |

The source/engine Node differentials and fresh production OriginalFlow/recovery
correspondence verify these semantic claims; this report does not assert that
the differing assembly pairs are identical.

## Original and emitted target method

The original below is the evaluation-only callback with its parameter type
annotation erased. The emitted callback is the exact published method.

Original:

```js
function handleMessage(message) {
  switch (message.type) {
    case 'loadFile':
      if (message.fileUri) {
        loadFile(message.fileUri);
      }
      break;
    case 'connectServer':
      if (message.serverIp && message.serverPort && message.ca && message.cert && message.privateKey) {
        connectToServer(message.serverIp, message.serverPort, message.ca, message.cert, message.privateKey);
      }
      break;
    case 'sendMessage':
      if (message.message) {
        sendMessageToServer(message.message);
      }
    case 'closeServer':
      if (message.message) {
        closeServer();
      }
      break;
  }
}
```

Emitted:

```js
handleMessage = function(message) {
  let $value3;
  $loop0: switch ($value3 = message.type) {
    case "loadFile": {
      if (!message.fileUri) {
        break $loop0;
      } else {
        loadFile(message.fileUri);
        break $loop0;
      }
    }
    case "connectServer": {
      if (!(message.serverIp && message.serverPort && message.ca && message.cert)) {
        break $loop0;
      }
      if (!message.privateKey) {
        break $loop0;
      } else {
        connectToServer(message.serverIp, message.serverPort, message.ca, message.cert, message.privateKey);
        break $loop0;
      }
    }
    case "sendMessage": {
      if (!message.message) {
      } else {
        sendMessageToServer(message.message);
      }
    }
    case "closeServer": {
      if (!message.message) {
        break $loop0;
      } else {
        closeServer();
        break $loop0;
      }
    }
  }
};
```
