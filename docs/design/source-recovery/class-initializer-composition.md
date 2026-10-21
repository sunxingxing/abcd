# Class initializer composition checkpoint (2026-10-21)

Baseline: `8f50e7e8`. This checkpoint is **partial**: CustomScanViewModel
completes; Hypium passes initializer ordering but reaches a captured-iteration
close obligation; the stored instance-initializer role is not implemented.
No new source-language premise or contract version is introduced.

## Bytecode diagnosis

| Record | Original input | Interpretation and result |
|---|---|---|
| o32/15324 | m15697: class creation PC13; internal self store28; prototype32; initializer method40; explicit-receiver call51; environment cleanup; public module store62 | Initializer m16054 defines `context = new Map()`. It does **not** call a Hypium static method. Its global read/construction made `ClosedClassFields` refuse. The uncaptured initializer cannot observe/pass out the fresh receiver: all receiver uses are own field-definition bases. The new private-receiver proof discharges this observation obligation. |
| o32/51260 | m51442: class creation13; self slot1 store28; prototype32; method m51746 creation40; **slot0 store45**; module publication59 | This is a stored **instance** initializer, not an immediate static call or an ordinary function. Constructor m51713 loads incoming slot0 and performs `callruntime.callinit` with its receiver. m51746 defines seven string-valued fields (`ANY`, `ANY_STRING`, `ANY_BOOLEAN`, `ANY_NUMBER`, `ANY_OBJECT`, `ANY_FUNCTION`, `MATCH_REGEXS`). It still refuses at `CreationContextError::kKind`, m51746/creator51442 PC40. |
| o25/40458 | m40834: creation24; prototype read; getter/setter/method installations; static initializer method309, call320; private cache and module publication; decorator call and later publication | The initializer defines `instance = undefined`. The member installations were already independently projected but the combined initializer projection did not include their ordered effects. This caused `ClassInitializerError::kFlow`. Composition, layout and printing now consume the combined proof; the entire record completes. |

For Hypium, trying the existing full class-inner preparation route was
insufficient: preparing the member families itself hits multiple-exit control.
That experiment is not retained as an admission rule. The new proof instead
states the narrower semantic requirement that actually justifies moving the
private self initialization past an **unobserving** initializer.

## Private receiver proof

`BuildPrivateClassReceiver` and `VerifyPrivateClassReceiver` use fresh common
SSA/effect proofs and copy/phi identity, not names, PCs, class sizes or opcode
sequences. The owner first verifies the real initializer occurrence, class
receiver, zero-parameter ABI and the Closed reader's absence of captures.
Unfulfilled child capture barriers also refuse (including buffered literals
whose method contents have not been proven noncapturing), not just explicit
function creations. This route does not borrow a child-family certificate.

The receiver may only be transported through identity copies/phis and consumed
as the base of an original `DefineField`. A receiver read, argument, call,
return or stored receiver payload refuses. Incoming capture closure is the
owner's independently checked prerequisite, rather than an opcode restriction
on otherwise private local expression evaluation. Environment capture/exposure
also refuses: a new callback/class could export an incoming self cell without
using the receiver SSA value. The semantic effect domain distinguishes this
from private lexical accesses and environment stack state changes. Explicit
debugger observation and suspension are also excluded by their semantic effects. Thus RHS calls may
run user code, but cannot expose this new class or invoke one of its methods.
The allocation-to-internal-store privacy proof in `FindClassSelfStores` is
unchanged. The original public binding remains unpublished until its store.
Nothing is declared initialized merely from a matching cell number/name.

All original ordered effects form a must-reach chain: no omitted, bypassed,
replayed or reordered event is accepted. Each throwing operation must have
exactly its original caller exit, origin and before-throw effects. The
independent verifier reconstructs these lists and rejects changed evidence.
Native field TreeFlow/recovery still verify the RHS and every field definition;
this certificate grants no printing permission by itself.

The existing successful ClosedClassFields path remains first. Only its typed
unsupported observation result tries this second proof. Failed-attempt scratch
and work are reported and charged before attempting it. Resource/work errors
never become a successful proof or a different semantic admission.

## Installed members followed by a static initializer

`ProjectClassDefinitions` already proves the private installation corridor.
`ProjectClassMembers` now includes those certified effects in its combined
predecessor/successor proof through the initializer call. All original class
uses must be owned installations or observations dominated by successful static
initialization. Escape, intervening effects, bypasses and wrong receiver refuse.

`CreatorClasses` builds one shared projection per creator and its verifier
recomputes one from immutable inputs. Layout and preparation borrow it rather
than rescanning the CFG for every class. `ClassInstallationOrder` retains the
initializer and ordered original definition IDs. Discovery verification checks
that inventory independently; the printer requires the same inventory before
emitting installed members together with static fields. The existing evaluation
verifier independently rebuilds the combined projection, and ordinary
TreeFlow/recovery remain mandatory. A standalone layout request computes its
own proof. No ordinary interleaved/user effect acquires an installation role.

Pinned frontend `es2panda/ir/base/classDefinition.cpp:410-422` initializes the
inner name, installs missing members, then runs static initialization.
`compiler/core/function.cpp:167-213` compiles fields/blocks in declaration order
and skips method definitions during initializer execution. This justifies
printing the proven installations before the static fields. Runtime field
semantics remain own definitions: `ets_runtime/ecmascript/interpreter/
interpreter-inl.cpp:7600-7675` and `stubs/runtime_stubs-inl.h:3450` route to
DefineField, not an inherited setter.

The added work is O((V+E) log V), plus linear use/effect/exception lists and
sorting the existing definition inventory. Storage is prepaid. There is no
per-class whole-graph scan on the creator pipeline.

## Remaining targets (not claimed complete)

* **Hypium:** m15918 (`handleItResult`) now stops at
  `PhasedMethodError::kControl > ExceptionControlError::kMultipleExits`,
  Try1 PC283/H308, normal exits297→319 and306→310. The standalone close probe
  proves entries, ports, private state and priority. Split body-protection Try3
  fails dispatch coverage at PC84: its property read throws through H259
  (`poplexenv; throw`) before H265. The captured `description` binding requires
  the per-iteration cleanup/activation correspondence. No such edge is erased
  or claimed by the initializer proof. Parked records42397/46008 were untouched.
* **ArgumentMatchers:** needs a separate stored-instance-initializer certificate
  and constructor event projection, not widening ordinary FunctionKind::kNone
  or treating a stored method as an immediate static invocation. Pinned
  `interpreter-inl.cpp:1499-1506` says CALLINIT has CALLTHIS0 semantics.
  `es2panda/compiler/core/function.cpp:219-233` invokes it at base-constructor
  entry; `ir/expressions/callExpression.cpp:123-133` invokes it after successful
  super for derived constructors. Recovery must bind the private stored
  function by environment identity, exclude overwrite/escape/extra invocation,
  match once per construction and consume its DefineField events as native
  fields. Computed keys need their class-definition activation, not recomputing
  a key per instance. No new language premise appears necessary; none is assumed.

## Newly complete source (evaluation comparison only)

The paired `CustomScanViewModel.ets` is used only to evaluate the result. Its
relevant declarations are:

```ts
private static instance?: CustomScanViewModel;
static getInstance(): CustomScanViewModel {
  if (!CustomScanViewModel.instance) {
    CustomScanViewModel.instance = new CustomScanViewModel();
  }
  return CustomScanViewModel.instance;
}
```

The corresponding emitted class excerpt is:

```js
static getInstance() {
  if (!$local3.instance) {
    $local3.instance = new $local3();
  }
  return $local3.instance;
}
// Other original members precede the final original static field definition.
static instance;
```

The outer sequence remains `const $value2 = class CustomScanViewModel { ... }`,
then `$local3 = $value2`, the module's `let CustomScanViewModel = $value2`,
`Observed(CustomScanViewModel) || CustomScanViewModel`, and the two original
publication updates. The static undefined definition stays before all of these.
Full output (not a stub) is in
`build/review/class-initializers/new-source/o25/sources/customscan/src/main/ets/viewmodel/CustomScanViewModel.mjs`.

## Tests and SDK evidence

* `private_class_receiver_test`: observed receiver, [[Set]] substitution, wrong
  callee/receiver, missing/changed exceptions, forged/reordered evidence,
  repeated-cycle, copies, block splits; 200/201/400/800 and exact byte/work
  boundaries. At800: straight graph255675 bytes/52858 work; split858075/182458;
  copied split858442/182543.
* `class_static_installations_test`: valid and split corridors at
  200/201/400/800, exact byte/work limits; escape, barrier, wrong receiver,
  missing definition/order and forged evaluation disposition. At800:
  2071465 bytes/667127 work; split2072456/667347.
* `class_initializer_composition_source_test`: ten actual SDK fixtures,
  default and O0. Five fixtures publish in at least one mode; 56 engine-output
  Node differentials cover values, getter/setter and RHS order, inherited
  setters, RangeError/TypeError/SyntaxError and class reentry. Five fixture
  families remain typed coverage gaps (stored base/derived initializer,
  computed-key opcode, uncaptured static block and the additional public-TDZ
  fixture). Default-mode cleanup refusals on `inner_outer`/`reentry`/`outer_reassigned` are
  recorded explicitly. Refused variants are never counted as differentials.
* No existing source spelling expectation changed: the printer only consumes
  the new verified combination. The static/class expectation search is retained
  under `build/review/class-initializers/printed-expectations.txt`; relevant
  source tests ran in the full sweep.

SDK identity: es2abc SHA256
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`;
ark_disasm SHA256
`a82e0c92a6726eb5c515f96c1731984e2a3938a6ce98dc540c7b5e7c08860676`.
Commands use `--module --merge-abc --extension=js`, matching record names and
source-file labels, at default optimization and separately `--opt-level=0`.
Comparison uses `tests/tools/grouping_bytecode.py`'s SDK/disassembly helpers.
No registers, branches, exceptions or metadata were normalized. The
outer-reassigned fixture retains the first class, overwrites the public C
binding with17, and checks that its initializer and static method still see the
inner class. Getter throws and replacing Map during its argument evaluation
are included in the Node scenarios.

Fixture method comparisons: installed-accessors6/6 exact at each level;
private-receiver5/5 at each level; O0 inner-outer2/4, outer-reassigned3/4 and reentry4/5.
Total **31/35 method bodies exact** across seven published recompilations;
whole files differ because of literal/string offsets, so none is called
whole-file identical. The other four bodies retain class-expression/cache
copies and native lexical guards; inner-outer also changes the guard's binding
name. The original outer writes and public commits stay after static execution.
Every command, hash, method result and raw diff is in the test's `summary.json`
and companion `.diff` files under the build directory.

### Actual CustomScanViewModel recompilation

Both SDK modes compile the new output. Identifier export-alias quotes are
removed only for SDK parsing (`as "default"`→`as default`, etc.); bindings and
export names are unchanged. All33 original method headers are present.
O0 **18/33 bodies are byte-identical, including the static initializer**.
The other15 are not counted equal:

| Method | Raw difference and semantic correspondence |
|---|---|
| getInstance | Private cache slot0→1 and guards on the native lexical binding; cache is initialized before any reachable invocation. |
| constructor | Explicit local initialization and an eliminated accumulator reload; instance write/call order retained. |
| closeScanResult | Dead zero load from `void 0`, then the same undefined store. |
| customScanCallback | Native negation expands the Boolean conversion and swaps exact branch bodies; short-circuit and effects retained. |
| detectFromPhotoPicker | Registers/private temporaries, async resume scaffolding and lexical guards; original awaits/rejections remain their recovered source await points. |
| regDisplayListener | Native captured local rather than debug-named receiver snapshot; same receiver stored once. |
| reqCameraPermission | Registers, explicit negation and source return-await completion scaffolding; same calls/awaits and Boolean outcomes. |
| startCustomScan | Receiver capture guards, and `['catch']` emits a value-key load rather than a name-key load. Same immutable string key and saved receiver/callee. |
| stopCustomScan | Same `['catch']` key lowering difference. |
| updateCameraCompSize | String-concatenation temporaries/register allocation; original operand/effect order retained. |
| updateFlashLightStatus | Source local initialization, Boolean branch complement and registers; original open/close calls, callback and catch retained. |
| async display callback (#~@0>@1*#) | Source selection temps/branch complement, native await return/guards and register allocation; same branch effects/await sequence. |
| display restart callback (#~@0>@1**#) | Guard on an already-initialized captured receiver. |
| scan callback (#~@0>@2*#) | Guard on an already-initialized captured receiver. |
| func_main_0 | Native private locals and module alias layout, class-expression copies, decorator/logical selection lowering; all installations precede static initialization, publication and decorator application remain in order. |

Outside method bodies the record also changes ArkTS/ECMAScript language tags,
source/debug environment-name literals and the compiler's inferred constructor
ExpectedPropertyCount hint (0xf). These are not property definitions and do not
replace the actual field/descriptor checks. Literal/string offsets change too.

Default compilation is optimized whereas the original is unoptimized: all33
bodies differ. Those are **not** claimed equal; raw diffs are retained. The new
proof's decisive local comparison is the unchanged own-field initializer and
unchanged installation/static/public order. Node fixtures are ordinary-runtime
oracles; Harmony APIs in the complete application record were not executed.

## Acceptance results

Full build passes. Source-regression **65/65**. New tests **3/3**. Broad requested
sweep **215/221**; five known failures: family_source_cli, tree_structure,
try_tree, class_tree_source, try_tree_source. The sixth is nested_members800 in
class_self_initialization_scale: the enclosure helper reaches its resource
limit. Running that exact generated ABC against both baseline and new unit
binaries gives the same FamilyEnclosureError::kResourceLimit and SIGABRT.
The other five failures were reproduced with baseline binaries as well.

Cohorts: source604→**605/620**, previous541→**542/620**. Zero regressions and
zero previous-complete text changes. Only two records have manifest changes in
each cohort: o25/40458 completes, o32/15324 has the later refusal above.
Both newly emitted target files pass `node --check`.
Scratch evidence is under `build/review/class-initializers/`.

An additional 40-input source run measured maximum CLI peak RSS **40,992,768
bytes**. Summing the four largest individual process peaks gives a conservative
four-worker bound of **149,520,384 bytes** (runner storage is separate).
