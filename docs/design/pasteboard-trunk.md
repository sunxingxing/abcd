# Pasteboard Whole-Record Experiment

Pasteboard is the working corpus for extending the generic semantic trunk,
not a source template or a set of special-case writers. Both input paths in
`tests/corpus/pasteboard.json` have the same SHA-256. Pinning the complete
denominator prevents easy methods from replacing difficult records. The
framework `pasteboard_taihe_ani.abc` is a separate input, not this application.

## Inventory

The ABC metadata, not adjacent source directories, supplies this inventory.
All methods are included, including initializers and anonymous functions.

| Record entity | Record suffix | Methods |
|---|---|---:|
| 4092 | _ESConcurrentModuleRequestsAnnotation | 0 |
| 4144 | _ESSlotNumberAnnotation | 0 |
| 4182 | common/CommonConstants | 5 |
| 4466 | common/Logger | 6 |
| 4767 | entryability/EntryAbility | 12 |
| 5246 | entrybackupability/EntryBackupAbility | 4 |
| 5521 | pages/Home | 45 |
| 6897 | pages/PageA | 22 |
| 7640 | pages/PageB | 22 |
| 8383 | pages/PasteBoard | 37 |
| 9550 | pages/ProgressBar | 42 |
| 10857 | pages/ShareDitect | 24 |
| 11662 | @ohos.app | 0 |
| 11697 | @ohos.curves | 0 |
| 11735 | @ohos.matrix4 | 0 |
| 11774 | @system.app | 0 |
| 11811 | @system.curves | 0 |
| 11851 | @system.matrix4 | 0 |
| 11892 | @system.router | 0 |

There are 19 records and 219 methods. Zero-method metadata records remain
visible; they are not counted as recovered executable modules.

## Reproduction

```sh
build/abc_semantic /home/rabbit/sample/abc_with_code/pasteboard/modules.abc \
  --all-records > build/pasteboard-semantic.jsonl
```

The driver indexes once, streams one record/method at a time, and releases
method IR before advancing. It runs decode, typed semantics/CFG/SSA,
environment/value validation, region recovery, and AST lowering. Enum-backed
stage reports include record/method entities, failure codes, original PC and
the opcode at that PC. Stage-wide failures can use the default PC: the opcode
is context, not necessarily the blocker. Only the final summary certifies that
the survey finished; exit success does not mean source recovery succeeded.

AST-ready is deliberately not source-complete. The survey does not guess
constructor/function forms from method names, emit arbitrary wrappers, or
skip record initializers. Proven class/module ownership and ABI must precede
printer admission and record-level integration. Generated source positions
must come from printer emission, never from matching generated text.

The survey now proceeds through the body printer. `body_ready` rows contain
body text and body-relative PC spans. These fragments require the callable
owner's strict context, register declarations and ABI initialization; they
are not standalone functions. Record completeness remains false in this report.

## Integrated Body Path

`BuildSemanticBody` composes the shared IR, value/environment validation,
regions, AST and body printer. The existing callable owner invokes it for
fixed-parameter ordinary functions, base constructors, methods and accessors.
The owner still proves syntax, ABI, names and signature and initializes the
private register bindings. Unsupported cases fall back to the existing writers;
async, arrow, derived-constructor and non-simple signature admission is not
silently widened. This is incremental integration, not a second wrapper model.

Ordinary calls and explicit-receiver calls now retain typed arguments and
normal/exception effects in the IR. Source lowering admits an adjacent named
read/call or a read/spill/reload/call group in one basic block, with SSA
receiver-identity and callee-use proofs. Spill liveness includes phi inputs.
The getter runs exactly once, before argument reads, with the original receiver;
no `.call`, `.apply` or mutable Reflect intrinsic is synthesized. All fused PCs
share the generated call span. Other receiver calls still fail AST admission.

After this increment, all 219 methods were inspected: 130 fail semantic build,
17 fail proof, 26 fail AST, and 46 produce body source plus position maps.
Examples include Home's `uri` (5712), `curBp` (5878) and `rerender` (6295).
The integrated `abc_structure --source` run emits CommonConstants and Logger;
the remaining eight application records still fail source admission. These
record outputs mix generic bodies with existing verified structural writers.
The method spans are not yet relocated into record-wide source coordinates.

Both CLI targets compile. Only targeted sample generation/inspection was run;
runtime regression tests, full-corpus tests and RSS tracking remain paused.
Follow-up functional regressions must cover getter count/order, receiver
identity, indirect eval, saved-callee reuse/phi escape, thrown calls, and fused
PC spans before claiming behavioral verification of the new call path.

## Module and Value Expansion

The next increment produces 79 of 219 method bodies and maps (previously 46).
117 methods fail semantic construction and 23 fail AST lowering. No methods
in this run fail the intervening proof or printer stages; that is not evidence
that exception/capture semantics are complete.

- Named calls may now cross private slot copies and primitive/string constant
  assignments. A last-effect index and SSA definition links avoid per-call
  interval scans. The saved callee and fetched value must have exactly one use,
  including phi inputs. Receiver identity and basic-block boundaries remain
  mandatory. Fused PC origins retain their actual statement distances.
- Adjacent module loads and static-name Hole guards lower together to a live
  binding read with TDZ throwing behavior. The load is not asserted to be
  Hole-free in the original VM. A guard at another block entry, unguarded load,
  or handler region does not receive this proof. Both PCs map to the emitted
  read. The printer separately validates the owner's module slot; detached
  local-module reads still require declaration evidence.
- Empty objects, supported data-only literal buffers and constructor calls
  have typed IR/AST operations. Buffered objects reuse the existing checked
  literal parser/printer; unsupported literal tags are still rejected, not
  flattened or copied from sidecar source.
- Global name reads require the existing unshadowed-scope proof. Var accesses
  and global initialization are not treated as interchangeable operations.
  Survey fragments use only private `$` bindings and explicitly require
  unshadowed globals; actual record embedding repeats checks in its real scope.

The survey loads module metadata once per merged record from the ABC itself.
No sourceMap or adjacent source file participates. Remaining IR blockers
include 55 first failures at lexical loads, 16 at named environment creation,
12 SSA failures, 13 methods with exception regions, seven DEFINEFUNC sites,
five global-object loads, five super calls and four rest-argument operations.
These are first-failure categories, not counts of all missing instructions.

Compilation and targeted generation were performed; behavioral tests remain
paused. Before behavioral certification, add module TDZ/alternate-entry cases,
callee reuse across phi, getter effects around moved copies/constants, global
shadowing, object descriptors/prototypes and constructor new.target cases.

## Staged Call Regions

The current complete-inventory run produces 93 method bodies with PC spans;
117 methods still fail IR construction and nine fail AST lowering. All nine
AST failures are now DEFINEPROPERTYBYNAME operations, not receiver calls.
This remains method-body coverage, not complete-record or behavioral coverage.

`SourceStagedCall` owns a same-basic-block argument region. The printer emits
the named property reference first and evaluates the region inside the first
argument's arrow expression. This retains getter-before-argument effects and
the original receiver without consulting `.call`, `.apply` or Reflect. For
example, Home's `aboutToBeDeleted` (6596) now preserves the outer `delete`
lookup before evaluating the nested `id__` call; callback 5932 preserves the
logging getter before the error-message property read and string coercion.

The proof still requires single-use callee spill/reload and SSA receiver
identity. A stack of end-ordered intervals rejects crossing call regions;
properly nested intervals are admitted. Every interval is pushed/popped once.
Emission visits owned statements once, caps call nesting, and charges interval
storage to the AST budget. Getter and call PCs share the complete call range;
argument operations retain their own emitted ranges. Mapping rows are not
guaranteed to be PC-sorted, and no PC is reconstructed by searching source text.

SSA failures now preserve their typed underlying cause and the actual failing
instruction PC when available. This exposed nine initialization methods whose
first missing register effect was STMODULEVAR. Its validated operand and
accumulator-preserving store effect now enter SSA and semantic IR, but this is
not permission to hoist or invent a local export declaration. The remaining
three SSA failures identify two SUPERCALLSPREAD sites and one buffered-array
creation site. The other initialization methods advance to later semantic
failures rather than being counted as recovered source.

Both CLI targets compile and the same Pasteboard inventory was regenerated.
Runtime tests remain paused. Deferred regression coverage must include nested
getter/argument effects, non-callable getters, argument throws, receiver
mutation, callee phi escape, crossing regions, and one-origin-per-PC maps.

## Existing Data Fields

The current inventory produces 101 method bodies with PC spans: 117 remain
blocked in IR and one in AST. The one AST failure is CommonConstants'
`static_initializer` (4439), whose class ownership cannot be replaced with a
fresh-object assumption. Eight former DEFINEPROPERTYBYNAME failures now emit
through `SourceOwnStore`, including nested resource objects in UI callbacks.

Admission has two independent parts:

1. SSA identifies a same-block buffered-object allocation through the receiver
   copy. Since allocation, only private value preparation, fresh allocations
   and similarly proven field writes may occur. Any other operation invalidates
   this conservative freshness proof; arguments, proxies, escaped objects,
   arbitrary `this` and inherited class state are not admitted.
2. Before emitting text, the printer resolves the exact key in that buffer's
   checked data-only shape. The property must already exist with the ordinary
   object literal's writable/enumerable/configurable data attributes. Missing
   keys and unsupported literal forms fail, rather than consulting an inherited
   setter or synthesizing a mutable Object.defineProperty call.

The pinned `RuntimeStubs::RuntimeDefineField` uses CreateDataPropertyOrThrow
with all three attributes true (`ets_runtime/ecmascript/stubs/runtime_stubs-inl.h`).
Strict assignment to this proven existing own data property therefore preserves
the value update and attributes, even when Object.prototype has a setter with
that name. This reasoning does not apply to STOWNBYNAME or static initializers.

`ReadObjectBuffer` now returns the checked literal, keys and sorted key index;
the old buffer writer consumes the same result. Field checks group requests by
literal pool slot, decode each requested shape once, then binary-search keys.
No per-field full-buffer scans or semantic dispatch on generated strings are
used. Scratch and retained key storage share the field-proof budget.

Both tools compile and the full Pasteboard method inventory was regenerated.
Behavioral tests remain paused. Deferred cases include inherited setters,
`__proto__` data keys, missing keys, duplicate literal keys, allocation aliases,
call/heap-write escape, nested objects and static-field rejection.
`abc_semantic INPUT.abc METHOD_ENTITY --instructions` provides numeric decoded
operands and available literal-buffer data for inspection, not source output.

## Lexical and Function-Creation IR

The next inventory run retains 101 body outputs and one AST rejection, but
reduces IR rejections from 117 to 44. The other 73 methods now reach source
proof and are rejected there. This is IR coverage only: no new method or
record is counted as recovered source in this increment.

The shared IR now includes lexical loads/stores, named and unnamed environment
allocation, current/parent environment operations, DEFINEFUNC/DEFINEMETHOD and
the internal Hole load. It reuses the checked typed lifters for narrow, wide
and supported deprecated formats. Lexical addresses retain depth and slot;
deprecated stores retain their explicit source register. Environment allocation
retains slot count and optional scope-pool reference. Function creation retains
the regional method-pool reference, observable length, home-object input and
cache operand. None of these operands is converted into a guessed source name.

Separate lexical-cell and current-environment effect bits prevent a cell store
from being confused with either an ordinary property setter or an environment
switch. The pinned interpreter's LDLEXVAR reads the raw cell without a getter or
TDZ check. NEWLEXENV installs the environment and accumulator only on successful
allocation; its before-throw effects exclude that installation. Function
creation has conservative allocation/context-read effects until target
attributes resolve ordinary versus sendable capture. Each occurrence remains
in the IR at its original PC; creation is not deduplicated by method ID.

Source proof has deliberately not been relaxed. Its current accumulator pass
does not yet admit lexical/context operations or function creation: 52 first
proof failures are lexical loads, 14 are environment creation, and seven are
function creation. The next integration must connect environment topology,
creation-site capture rebasing and cell initialization to SSA value proofs,
then pass typed bindings to the AST/printer. Neither a successful lexical load
decode nor an environment topology result establishes a language-valued cell.
Hole/internal environment values must not leak into generated JS; sendable
targets must not be assigned ordinary captures. Class creation and exception
regions remain explicit separate blockers.

Both CLI targets compile and the complete Pasteboard inventory was regenerated
from ABC alone in `build/pasteboard-semantic-lexical.jsonl`. Runtime tests and
RSS tracking remain paused. Deferred regressions cover lexical format widths,
deprecated store sources, Hole versus undefined, allocation failure before
environment installation, shadowed/deep captures, environment joins, function
length versus parameter count, and ordinary versus sendable creation.

## Captured-Read Templates

The generic path now supports parametric captured reads through environment
binding, SSA language-value premises, region recovery, source AST and printer.
The 219-method report has 101 body outputs, 17 capture templates, 44 IR
rejections, 49 proof rejections and eight AST rejections. The 17 templates are
NOT added to the body count or reported as complete methods/records.

`VerifySemanticSource` defaults to rejecting unproven captures. Its explicit
`CapturedValues::kLanguage` mode uses the existing child-template contract:
environment analysis first resolves each read to a captured depth/slot, then
records that read's conditional Language premise for the SSA solver. Local
cells, stores, environment allocations and unresolved function creations do
not acquire this premise. Every captured read survives as a typed AST node,
including reads whose results are subsequently unused.

`PrintedTemplate` is distinct from `PrintedSource`. Its text contains zero-width
typed capture insertions rather than guessed binding names or string tokens.
Ordinary source printers reject these nodes. Template spans and insertion
coordinates are measured during emission and are explicitly template-relative;
they cannot be published as final source positions after substitution.

The callable owner now invokes the generic template path for supported ordinary
functions/methods and already validated ordinary arrows. It replays every
capture into the existing `CaptureBuffer` protocol. The parent `LeafSources`
path still rebases each occurrence at its actual creation site and verifies
initialized captured cells before source publication. Templates are reusable
syntax, not evidence that separate creations share bindings or function objects.
Async, nested creation, local-cell promotion and captured stores are not widened.

The survey schema is now 2, with a separate `template_ready` stage. These rows
carry `template_text`, `captures`, `template_positions`, `executable: false`
and explicit capture-binding/Language/global-scope requirements. For example,
EntryAbility callback 4973 retains a capture before its staged
`updateBreakpoint` call. Home callback 5739 retains four occurrences of the
same captured cell across branches, property writes and a dialog call. Neither
fragment is a standalone callable, and neither uses sidecar source/sourceMap.

Both CLI targets compile. ABC-only targeted generation produced
`build/pasteboard-semantic-captures.jsonl`; record generation still emits only
CommonConstants and Logger as source. The other application records remain
incomplete. Runtime tests and RSS tracking remain paused. Deferred checks must
cover ordinary-printer rejection, missing/failed capture sinks, per-creation
rebasing, uninitialized parent cells, unused/repeated captures, nested staged
call capture order, branch joins, arrow ABI restrictions and final span
relocation after insertion.

## Bound Template Coordinates

`BindSourceTemplate` now accepts one parent-proven `LocalCell` per emitted
capture and returns bound body text with relocated PC spans. It does not
perform capture rebasing or establish cell initialization: its caller must
discharge those obligations for this exact creation site and declare the
private parent-cell names without collisions. `LexicalName` centralizes that
name format with the existing production `LeafSources` emitter.

Template positions now include an emitted capture cursor. This distinguishes
the two sides of a zero-width insertion even when byte/line/column are equal.
The printer carries this cursor through ordinary spans, fused reads and nested
staged calls. The binder uses prefix sums of inserted identifier lengths for
byte shifts and a binary search for same-line column shifts. Each final
position clears its template cursor. It never searches generated JS for a
variable or reconstructs instruction origins from source text.

Binding preserves repeated captures, overlapping fused spans, zero-width
anchors and non-PC-sorted span order. Inputs, binding cardinality, insertion
ordering/coordinates and output budgets are checked before a result is
published; missing bindings do not produce placeholder code. Work is
O(text bytes + captures + spans log captures), with bounded text/map/prefix
storage. Scope/cell semantic validity remains a parent-proof precondition,
not something established by formatting a `$lex` identifier.

The inventory schema is now 3: capture insertions and template span endpoints
include their capture cursors. This increment does not add completed methods
or records. In particular, the record writer still does not retain nested
method span events through its entire output assembly; the new binder is the
explicit finalization interface, not a claim that record-wide maps are done.
Parent environment initialization and generic function-creation AST integration
remain the next functional gates.

Compilation and targeted Pasteboard generation remain the available checks;
behavioral tests and RSS tracking stay paused. Deferred binding regressions
include both sides of coincident insertion endpoints, multiple captures on a
line, newlines between captures, empty/repeated bindings, nested call ranges,
coordinate overflow, resource exhaustion and per-creation cell mismatches.

## Leaf Function Expressions

Function creation now passes the accumulator proof with its actual input:
DEFINEFUNC does not read the accumulator, whereas DEFINEMETHOD consumes its
home object. The generic source AST admits the ordinary DEFINEFUNC shape as
a typed expression; method/home-object placement remains unsupported by this
new source rule. No decoded opcode is reconstructed from semantic operands.

The callable owner has a typed `FunctionCreation` entry point alongside the
existing decoded-instruction API. Regional target resolution, runtime name,
observable length, formal parameters, ABI, function kind, module ownership,
sendable rejection and arrow role validation still belong to that owner.
The printer's leaf bridge rejects external captures and buffers a complete
verified child expression before adding it at the original creation PC.
Child source can use the generic body path or an existing verified fallback;
successful parent emission is not proof that every descendant used generic IR.

One-level expansion is an explicit borrowed `leaf_methods` capability in the
source context. Owners enable it only with their existing method lookup;
created-child contexts do not inherit it. This bounds expansion without an
implicit recursive traversal, recursion-depth magic number or thread-local
state. Each emitted expression still creates a fresh runtime function object.
The survey builds the method lookup and complete arrow role index once per
file; failed role evidence never authorizes arrow emission.

The current 219-method report contains 101 body outputs and 18 templates,
with 44 IR, 17 proof, 21 AST and 18 printer rejections. Previously there were
17 templates and 49 proof rejections. ProgressBar method 10305 now prints its
`observeComponentCreation2` callback as a real arrow expression containing
SymbolGlyph creation, resource-object field initialization and `fontSize`.
The parent still requires one captured cell and remains a template. This
does not invent a body for unsupported child functions or count stage progress
as complete-record recovery.

The parent DEFINEFUNC PC spans the emitted child expression, including its
newlines. Child-internal method/PC spans are not yet propagated through the
legacy callable wrapper, so record-wide nested provenance remains incomplete.
General capturing children, local environment initialization, nested creation
graphs, class methods and sendable environments remain separate gates.

Both tools compile and targeted ABC-only generation produced
`build/pasteboard-semantic-functions.jsonl`. Record output still contains only
CommonConstants and Logger source files. Runtime tests and RSS tracking remain
paused. Deferred regressions include same-target fresh identities, recursive
and mutually recursive creation refusal, child capture refusal, arrow role
conflicts, module-owner mismatch, length/formal mismatches, child failures
before publication and getter-before-function-argument ordering.

## Local Initialization and Parent Captures

The generic source proof now uses register SSA value categories directly for
every operand read; it no longer requires the legacy accumulator scanner to
admit each instruction first. Unknown entry registers/accumulator, Hole and
internal environment values still cannot be read by emitted operations. The
old scanner remains available to legacy writers. A NEWLEXENV result may be
omitted only when no read exposes it, not by reclassifying it as a JS value.

`ProveLocalAccesses` indexes cells by environment/slot and records typed loads,
stores and allocation sites. A local read requires allocation and a preceding
store in its own basic block, with no function creation since allocation.
All stores then undergo the shared SSA Language check, including stores whose
cells are not subsequently read by the parent. This conditional proof cannot
bootstrap a read-before-initialization cycle. Cross-block initialization,
cell phis and post-capture loads remain conservative rejections. Sorting and
indexed lookups cost O(I + L log L), not a graph walk per cell; retained and
temporary vectors are budgeted, and local proof work reduces the value-solver
quota.

The AST contains typed local reads/stores and a cell declaration list. Source
uses `$lex` cells for ordinary parents and `$inner` cells for templates to
avoid shadowing later-inserted parent captures. Conflicting private names,
formals and self/class names are rejected. Declarations may be hoisted only
because no admitted read can observe their initial undefined value. Named
environment literals still pass the shared scope metadata validator before
omission; its header is now public so CLI and production paths use the same
certificate. Environment operations retain zero-width PC anchors under the
existing no-debugger/GC-OOM-observability contract.

The function printer can now consume the same-method proof while emitting a
child template. Each capture is rebased at the actual creation site and must
resolve to a parent local allocated and assigned earlier in that same block.
This first slice admits only the first capture creation since allocation;
later creations and captured parents remain blocked. The child independently
proves Language reads/stores. Bound names stream through the parent printer,
so the parent creation span includes their actual bytes and line positions.
There is no guessed capture name, temporary executable placeholder or search
through generated text. One-level child expansion remains unchanged, and
child-internal PC maps are still not propagated through callable wrappers.

In `build/pasteboard-semantic-parent-captures.jsonl`, the 219 methods now have
44 IR, six proof, 24 AST and 26 printer rejections, plus the unchanged 101
bodies and 18 templates. Eleven methods passed their previous proof-stage
blocker. EntryAbility `onWindowStageCreate` (5192), for example, passes local
environment initialization and reaches its child creation at PC 77; the child
still fails source admission. Thus this increment does not add a complete
method or record. CommonConstants and Logger remain the only record source
outputs in the targeted run.

Both tools compile; only ABC-only targeted generation was performed. Tests
and RSS tracking remain paused. Deferred regressions must cover uninitialized
and cross-block cells, internal env aliases, named-scope corruption, captured
stores of Hole, creation before initialization, multiple creation sites,
parent/template name collisions, branch phi values, unreachable paths and
failure after partially emitting a child into the unpublished parent buffer.

## Private Object Identities Across Unrelated Effects

The own-field AST gate now tracks same-block buffered allocation identities
through SSA slot copies instead of rejecting every intervening user effect.
A getter on an unrelated receiver, an unrelated lexical read, or function
creation cannot expose an object held only in private VM registers. Field
updates on that object therefore remain admissible, subject to the unchanged
printer proof of an existing writable/enumerable/configurable own data key.
The pinned DEFINEPROPERTYBYNAME handler calls DefineField; RuntimeDefineField
uses CreateDataPropertyOrThrow, not inherited-setter assignment semantics.
Missing keys and arbitrary receivers are still rejected.

`PrivateObjects` retains one optional allocation instruction per SSA value.
Copies propagate identities in instruction order; phi and cross-block values
are deliberately not promoted. Any non-copy use of a tracked reference
conservatively invalidates all earlier identities, except an admitted own
store into another private object. Invalidating the entire epoch also covers
transitive containment without building a heap graph. A lexical/module store,
call argument, return, or property read of the private object therefore cannot
silently preserve freshness. Validation of all own-store shapes precedes
publication of printer output. Closure creation reads the environment, not
arbitrary VM registers; storing an object into that environment already
invalidates its identity.

For I instructions, R SSA reads and V SSA values, the new pass uses O(V)
budgeted scratch and O(I log(I + R + 1) + R) work with the existing indexed
SSA read/write API. There are no per-object suffix or alias-chain scans.
This is a conservative local escape proof, not general heap alias analysis.

ABC-only inspection in `build/pasteboard-semantic-private-objects.jsonl`
advances ProgressBar methods 10166 and 10583 from AST rejection to capture
templates. In 10166, PC 40 and PC 57 now emit the existing `value` and `type`
field updates after the original property reads, with distinct generated
spans. Method 6678 passes its field AST gate but remains rejected at child
creation PC 107. Totals are 44 IR, six proof, 21 AST and 27 printer failures,
101 bodies and 20 incomplete templates. No complete record was added;
CommonConstants and Logger remain the only record source outputs.

Both CLI targets compiled and targeted ABC-only generation completed. Tests
and RSS tracking remain paused. Deferred negative regressions include alias
publication through lexical/module/property stores, getter access to the
tracked object, nested-container publication, copy chains, slot overwrites,
phi/cross-block aliases, unknown/missing keys, `__proto__`, and resource
exhaustion. Positive cases must retain unrelated getter/closure effects and
the original field PCs without moving evaluation or invoking mutable global
Object/Reflect helpers.

## Descriptor-Derived Source Paths

Source paths are now separate from entity-qualified diagnostic archive paths.
`PlanRecordSourcePaths` retains record ordering and descriptor directory/file
components, permits interior dots, and omits the unconditional entity suffix.
For example, Logger source is published at
`sources/com.example.pasteboard/entry/ets/common/Logger.mjs`; the old escaped
`records/.../Logger~r00001172.record.json` and declaration archive remain stable.
Percent escapes protect empty/traversal components, separators and unsupported
bytes. This is descriptor evidence, not proof of the original project root or
file extension: no `src/main` directory or `.ets` suffix is invented. Unicode
names still use byte escaping. Debug source-file provenance and original
project-layout reconstruction remain follow-up work.

The exporter plans all source stems before creating its staging tree and
rejects duplicate or ASCII-case-folded duplicate paths instead of overwriting
or guessing names. Sorting a bounded pointer index takes O(N log N * L) worst
case for N records and maximum path length L; strings and both vectors share
an explicit 16 MiB retained-planning budget. Per-path temporary construction
is additionally bounded by the existing component/path limits. Filesystem
file/directory conflicts fail the staged export; nothing is published in that
case. The `.mjs` extension continues to denote the actual emitted JS format.

Manifest `planned_source` records the intended destination even for rejected
methods/records; only a successfully emitted record gets `source`. The targeted
Pasteboard export still publishes CommonConstants and Logger only. Method
coverage and source-style fidelity have not changed in this path increment.
The engine consumed only modules.abc, not source files or source maps.

Both CLI targets compiled and the sample export was inspected. Added path
regression cases cover ordinary bundle dots, distinct names, duplicates with
different entity IDs, case-folded collisions, traversal/escaping and exhausted
budget; these tests remain unexecuted under the user's testing pause. RSS
tracking remains paused as well.

## Work Order

1. Use the full inventory to identify shared typed semantic gaps. The initial
   survey admitted 27 methods to AST and rejected 192 during semantic build.
   String loads were the first unsupported instruction in 26 methods. Add
   typed string-pool references through IR, AST and printer first, preserving
   UTF-16 escaping and keeping property-name restrictions off string values.
   After this increment, all 219 methods were surveyed again: 30 reached AST,
   one failed AST lowering, and 188 failed semantic build. This is stage
   coverage, not a claim that 30 methods or any complete record were emitted.
2. Extend shared calls with explicit receiver/callee/argument sequencing,
   lexical environment/closure facts, object and DEFINE semantics. Never
   substitute a mutable `.call` lookup or repeat a property getter. Rerun the
   same complete denominator after each increment to expose the next blocker.
3. Extend exceptional control flow and resumable async regions with explicit
   effects; do not coerce them into normal-flow-only proofs. SSA failures need
   their own diagnosis, not blanket opcode whitelisting.
4. Feed class/module ownership, imports/exports and record paths into the
   trunk. Publish a record only when all required reachable bodies and its
   initializer are faithfully represented. Keep incomplete structural output
   explicitly separate from executable source.

Legacy writers remain a differential baseline, not an alternate definition
of generic-path success. Neither sidecar source nor sourceMap is an engine
input. Functional inspection is targeted at this experiment; full-corpus
tests and RSS tracking remain paused at the user's request.
