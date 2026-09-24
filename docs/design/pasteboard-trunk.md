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

## Forwarding Child Captures Through Parent Templates

`ResolveCreationCapture` now returns either a proven parent local cell or a
typed entry-chain capture obligation. It resolves the child's normalized
capture through the environment at the actual DEFINEFUNC instruction; it does
not copy the child depth unchanged or infer names from emitted text. A parent
template forwards an entry-chain obligation as a zero-width `SourceCapture`
at the current generated position, including the insertion cursor. Ordinary
source still rejects an unresolved external capture.

This closes the case where a child and its parent both reference a grandparent
cell. Binding a template's own local cell remains refused: both parent and
child templates currently name locals with the `$inner` namespace, so naive
substitution could shadow the intended binding. Ordinary parents retain the
existing same-block first-creation initialization gate for their `$lex` cells.
No recursion-depth capability, initialization condition, capture-store value
proof, or source-position provenance contract was relaxed. Forwarded events
share the preallocated parent capture quota and fail with a typed resource
error if it is exhausted; child-internal PC spans remain outstanding work.

In `build/pasteboard-semantic-capture-forwarding.jsonl`, ProgressBar's
ProgressListener (10802) advances from printer rejection to a parent template
containing its setTimeout callback. Both its direct capture and the child's
forwarded capture resolve to entry depth 1, slot 0. The parent PC 111 creation
span advances its capture cursor from one to two while enclosing the child
text. Totals are 44 IR, six proof, 21 AST and 26 printer rejections, 101 body
fragments and 21 incomplete templates. The callback body is the newly admitted
10583 field-initialization case, so this composes the preceding object-identity
increment into an actual parent function rather than an isolated leaf output.

Both CLI targets compiled; ABC-only survey and record generation completed.
CommonConstants and Logger remain the only complete record outputs. Runtime
tests and RSS tracking remain paused. Deferred regressions must cover nonzero
entry-depth rebasing, pushed/popped local scopes, local/template name shadowing,
ordinary-source rejection of an unresolved capture, adjacent zero-width
insertions, capture quota exhaustion, and failure after partial child output.

## Super Initialization Guards in the Shared IR

Instruction inspection now resolves method operands in the caller's pool
region and reports `method_entity` (or a typed `reference_error`). This connects
Home's paramsLambda (6678) at PC 107 to child 6650 without guessing from method
names or reading paired source. The child blocker is opcode 2046,
THROW_IFSUPERNOTCORRECTCALL, not an async operation.

The shared IR now consumes the existing typed `LiftSuperInstruction` result
for both immediate widths. It retains initialized/uninitialized/no-op guard
modes, register reads, original PC, and normal/exceptional guard effects. Super
calls themselves are not admitted by this increment. Importantly, pinned
`RuntimeThrowIfSuperNotCorrectCall` in runtime_stubs-inl.h rejects BOTH Hole
and undefined in initialized mode, and accepts either in uninitialized mode.
The interpreter delegates to that runtime implementation. A Language-only SSA
fact or captured-load premise therefore cannot justify removing this guard.

AST lowering erases an initialized check only under the existing must-string
or must-non-NaN-number SSA refinements, both of which exclude undefined/Hole.
It retains a zero-width PC anchor. Explicit no-op modes are also admitted;
other checks return the new typed `SourceError::kSuperInitialization`. There
is no replacement with an ordinary property access (which would change a
ReferenceError into a TypeError), and no mutable global ReferenceError helper
is introduced. Full captured-this initialization or faithful explicit guard
emission remains necessary for the three affected real methods.

The ABC-only survey `build/pasteboard-semantic-super-checks.jsonl` moves 6322,
6486 and 6650 from IR rejection to that AST diagnostic at PCs 17, 8 and 8.
Totals are 41 IR, six proof, 24 AST and 26 printer rejections, with unchanged
101 body fragments and 21 templates. This is semantic-model progress, not
additional source coverage or completion of paramsLambda. Both CLI targets
compiled. Tests and RSS tracking remain paused; deferred regressions include
undefined versus Hole, conditional captured facts, nonzero no-op flags, both
operand widths, malformed operands, phi joins dropping refinements, and PC
anchors for proven-inert checks.

## SSA-Proven Lexical Hole Guards

The generic AST now handles HoleGuard using its typed value slot and the
same-method SSA value fact, rather than requiring every guard to follow a
module load. An exact Language fact excludes Hole; it does not exclude
undefined, which correctly passes both pinned UNDEFINEDIFHOLE handlers.
Those handlers read the error-name register or string pool only on the Hole
path, so a proven inert guard must not introduce name evaluation. The AST
retains a no-op statement with its original PC. Adjacent module guards keep
their existing fused module-load mapping; other guards have independent
zero-width anchors. Unknown/internal values remain rejected by verification.

Captured-load facts are still premises, not unconditional initialization
proofs. Removing a guard in a template does not discharge its capture event
or let the template become executable. Binding must still pass the parent's
environment and initialization checks. SuperCheck remains separate because
its initialized mode also rejects undefined.

`build/pasteboard-semantic-lexical-guards.jsonl` adds templates for 5684 (Home
construction callback), 8692 (PasteBoard styled-string callback), and 10417
(ProgressBar unlink callback). Their former blocking PCs 8, 65 and 67 remain
zero-width spans after the first capture insertion. All three artifacts are
explicitly non-executable. Totals are 41 IR, six proof, 21 AST and 26 printer
rejections, 101 body fragments and 24 capture templates. Both tools compile,
and targeted record export still emits only CommonConstants and Logger.

No runtime tests or RSS measurements were run. Deferred regressions cover
register versus accumulator guard values, undefined acceptance, actual Hole
rejection, unknown/phi facts, conditional error-name reads, capture binding
failure, and independent versus fused guard PC spans.

## Method-Scoped Template Locals

Template local names now include the owning ABC method entity, environment
identity and slot (`$inner<method>_<environment>_<slot>`). Ordinary parent-cell
names retain the existing `$lex<environment>_<slot>` ABI used by legacy capture
emission and `BindSourceTemplate`. Both are formatted into a fixed-size buffer
derived from the typed ID widths, without name parsing or semantic string tags.

The generic child capture sink can consequently bind a proven parent local
inside a parent template, using exactly the same formatter as that parent's
local declarations, reads and stores. Previously it rejected this case to
avoid shadowing a child's identically named `$inner` slot. The unchanged
first-creation, same-block store/initialization proof still applies. Method
IDs are sufficient only under the current same-file, one-level child expansion
capability: recursive/repeated-method nesting cannot successfully expand.
Any future recursive expansion must introduce an emission-scope identity or
prove that no ancestor binding can collide. Do not merely remove that bound.

Both CLI targets compiled. The ABC-only survey at
`build/pasteboard-semantic-scoped-locals.jsonl` remains 101 bodies and 24
templates; no rejection PC changed and none of the currently successful
templates declares local cells. Thus this change removes a known composition
restriction but has no demonstrated additional Pasteboard source coverage.
It must not be reported as an end-to-end capture-binding verification.
Deferred regressions include equal environment/slot IDs in distinct methods,
parent-template local capture, ordinary `$lex` ABI compatibility, same-method
nesting rejection, private-name collisions, maximum-width IDs, and declaration
versus insertion consistency. Tests and RSS tracking remain paused.

## Module Binding Context Through the Generic Path

The generic proof now distinguishes guarded module reads from owner-proven
initialized local reads in typed `ModuleReadFact` records. Its optional
initialization premises are sorted unique instruction IDs; validation rejects
out-of-range, duplicate, unsorted or non-local-load sites. `BuildSemanticBody`
and `BuildSemanticTemplate` obtain these premises only from the existing
record-owned `ModuleExports` state, after checking the slot-count contract.
The retained premise vector is charged alongside the live semantic method.
It is not synthesized from local export names, import metadata or a later
record store. This connects the legacy owner's initialization contract to
generic SSA and AST lowering without introducing a second module namespace.
The initialized versus guarded distinction also keeps guard PC fusion honest.

The per-method survey previously gave every callable an empty module slot
scope, incorrectly rejecting valid guarded local loads at the printer. It now
builds `ModuleExports` once per record to recover VM slot identity, including
adjacent equal-name export aliases. It never calls WriteStore, so every slot
remains uninitialized in this detached survey and no initialization premise
is claimed. Source fragments/templates explicitly report
`requires_module_bindings`; actual record publication remains separately
gated by complete initialization and callable ownership.

`build/pasteboard-semantic-module-context.jsonl` advances five methods (7089,
7832, 8580, 9748 and 11055) from printer failure at PC 22 to capture templates.
For PageA 7089, the local binding read retains its PC span and the combined
parent/child output carries six unresolved capture events. Totals are 41 IR,
six proof, 21 AST and 21 printer rejections, 101 body fragments and 29 templates.
The coverage gain is from supplying valid module binding identity to the
survey, not from pretending those exports are already initialized.

Both CLI targets compiled and ABC-only survey/record generation completed;
CommonConstants and Logger remain the only complete record source outputs.
Tests and RSS tracking remain paused. The new initialized-premise API still
needs focused regressions for a valid unguarded local load, uninitialized and
external loads, stale/wrong-method premises, duplicate IDs, alias slot counts,
budget exhaustion, and initialized-load-plus-guard PC mapping.

## Rest Parameters Through the Generic Function Body

COPYRESTARGS and its wide form now lift through the existing validated
`RestCopy` parser into semantic IR. Effects explicitly include argument-frame
read and allocation, with exceptional allocation completion; copying rest
arguments is not an iterator call or a heap getter. SSA already describes its
normal result as a language value. AST lowering requires the existing
same-body/frame `ProveRestPrologue` certificate, rather than trusting the
opcode or an arbitrary printed parameter name.

The production callable writer now attempts the generic path for both fixed
and proved rest signatures. `BuildSemanticBody`/`BuildSemanticTemplate`
recheck the supplied rest binding against the complete prologue proof. That
proof requires the ABI-copy prefix, one rest copy at the matching argument
index, the expected store, no later reads of the replaced original ABI slot,
no repeated copy, and no branch reentry into the prefix. The printer emits a
read of the owner's validated native rest formal. The pure copy prefix makes
native parameter-entry allocation valid under the existing OOM/debugger
observability contract; no arguments.slice/global helper or second array
allocation is introduced. The rest opcode keeps its source assignment PC.

Targeted export to `build/pasteboard-rest-source` now sends Logger's four rest
methods (4604, 4631, 4658, 4685) through generic body emission, keeping their
`...args` signatures. Compared with the preceding record output, their member
calls and returns now have the generic printer's form. This migrates actual
record bodies away from the legacy writer; it does not add another complete
record. CommonConstants and Logger remain the only record outputs.

The detached all-method survey deliberately has no rest-formal owner
certificate yet. It now lifts those four methods through IR/SSA and stops at
AST admission, rather than claiming a wrapper it has not proved. Thus
`build/pasteboard-semantic-rest.jsonl` reports 37 IR, six proof, 25 AST and 21
printer rejections, 101 detached body fragments and 29 capture templates.
The survey denominator is unchanged; its owner-context gap should not be
confused with the production record path's capability.

Both tools compiled and ABC-only exports were inspected. Runtime tests and
RSS tracking remain paused. Deferred regressions cover repeated copies,
wrong rest index/frame, malformed narrow/wide operands, branch reentry,
original ABI slot reuse, parameter-name collisions, arrays' fresh identity,
and PC mapping through the native rest wrapper.

## Embedded Source-File Evidence

Metadata indexing now retains the optional class SOURCE_FILE entity lazily
instead of validating and discarding it. The fixed-size optional offset is
covered by the index's RecordInfo admission accounting. Structural JSON emits
its decoded filename as `source_file`, or null when absent. This is a textual
metadata reference, never permission to read that host file.

The record structure writer additionally aggregates class and method debug
filename evidence into `source_file_evidence`: state 0 is missing, 1 is one
observed name, 2 is conflicting names, and 3 is incomplete decoding. Only
state 1 publishes the shared filename. Missing debug tables do not invent
names; malformed optional debug evidence cannot be treated as an empty valid
table. File-change/read failures remain fatal. Embedded source-code references
are not dereferenced. The collector retains one filename and one method's
bounded debug payload at a time, skips repeated consecutive entity references,
and compares textual data directly without a quadratic unique-name search.
Existing detailed per-method evidence remains available for conflicts.

The targeted export `build/pasteboard-source-consensus` confirms all 19
Pasteboard records lack class SOURCE_FILE tags. Their method tables provide
one consistent debug filename for each of the ten application records; the
other nine records have no filename evidence. PageA's encoded name is
`entry|entry|1.0.0|src/main/ets/pages/PageA.ts`, and the other application records
have corresponding names under common, pages, entryability and
entrybackupability. This recovers actual ABC-contained directory/name evidence,
not a guessed `src/main` layout or copied evaluation source.

The encoded `.ts` name may describe a compiler intermediate and does not prove
the original `.ets` extension. The pipe-delimited prefix has not yet been
promoted into a verified path-format profile. Therefore generated `.mjs` paths
remain descriptor-derived; no silent extension replacement or host-path
interpretation is performed. Filename evidence integration into the path
planner and original-language reconstruction remain unfinished.

Both tools compiled and targeted structure/source generation completed. No
runtime tests or RSS tracking were run. Deferred regressions cover optional
class tags, duplicate tags, invalid string references, matching and conflicting
method filenames, empty names, malformed debug tables, surrogate text,
resource exhaustion and hostile path components.

## Transitive Capture Classification in the Survey

The detached method survey now prints a parametric artifact first and
classifies its actual capture list. Previously it selected template mode only
when the parent's own SSA proof contained captured reads. That missed parents
whose nested callable alone uses the enclosing environment: ordinary printing
then refused a valid forwarded child capture at its creation PC.

An empty final capture list can become a body fragment; any capture event
keeps the result an explicitly non-executable template. This uses typed
printer artifacts, never a scan for missing expressions in JS text. Local
initialization, method-scoped local naming, child-role validation and the
one-level expansion capability are unchanged. Production callable ownership
is not inferred from the survey classification.

`build/pasteboard-semantic-transitive-captures.jsonl` now includes parent
templates for Home 5767 (four child-origin capture insertions at the callback
created at PC 126) and PasteBoard 8636 (one at PC 123). The whole creation spans
retain the capture cursors and both reports remain `executable:false`.
Totals are 37 IR, six proof, 25 AST and 19 printer rejections, 101 detached
body fragments and 31 templates. No complete record is added by this survey
fix. Both tools compiled; only ABC-only generation/inspection was performed.
Tests and RSS tracking remain paused. Deferred cases include parent-only,
child-only and mixed captures, children with no captures, quota exhaustion,
and rejection after partially emitted nested callable text.

## Buffered Arrays Through the Trunk

Modern `CREATEARRAYWITHBUFFER` narrow/wide forms now share a typed decoder
retaining the literal pool slot and cache slot. Register effects, value-kind
analysis, semantic IR allocation effects and AST assignments consume this
model. The source printer reuses the record reader and scalar literal writer
to emit a fresh dense array at the original instruction, preserving its PC
span. It does not hoist a shared mutable array. Primitive payloads retain
numeric bits and UTF-16 text; unsupported holes and method-bearing payloads
are rejected rather than rewritten. The deprecated global-index form remains
outside this modern regional-pool decoder.

Both CLI targets compiled. ABC-only inspection recorded in
`build/pasteboard-semantic-buffered-arrays.jsonl` shows method 6760 advancing
from SSA/IR rejection at array creation PC 85 to printer rejection at nested
function creation PC 110. This demonstrates that the array printer was
reached, not that the whole method was emitted. Counts are now 36 IR, six
proof, 25 AST and 20 printer rejections, 101 body fragments and 31 templates.
No complete function or record is newly claimed. Tests and RSS tracking
remain paused; deferred regressions include both operand widths, malformed
operands, regional resolution, unsupported payloads, numeric/string edge
cases, fresh identity, PC spans and resource exhaustion.

## Bounded Nested Callable Expansion

The generic printer can now propagate method lookup to grandchildren under a
single synchronous `CreationExpansion`. Legacy decoded-template entry points
remain leaf-only. The context owns a fixed 32-entry active method path and
rejects repeated ancestors before decoding them. This keeps method-qualified
lexical cell names unambiguous in nested scopes; sibling emissions remain
separate function scopes. Supporting recursive creation graphs still requires
a different representation and is not silently approximated.

Each child receives at most half the currently available scratch allowance;
its grant remains charged until its body, proofs and wrapper storage die.
Work grants draw from a shared non-refundable quota, including rejected or
fallback attempts. Output is capped by both the child grant and parent stream.
RAII restores ancestry and scratch grants on every exit. These are conservative
admission policies, not bytecode limits. Ancestor checks cost O(32) per emitted
creation, not a corpus-sized pairwise scan. This does not establish the full
process memory ceiling: the existing root/stream capacity accounting and
paused whole-process RSS verification remain separate obligations.

The base-function wrapper preserves nested lookup only with this context.
Existing typed capture events resolve one environment edge at each parent;
local initialization, first-creation restrictions and Language-store proofs
are unchanged. No captures are bound by rewriting printed names. Parent
creation PC spans include descendant text, but descendant-internal PC maps
still do not survive callable wrappers.

ABC-only output in `build/pasteboard-semantic-nested-expansion.jsonl` advances
Home `requestPermissionsFn` 6760 and PasteBoard `aboutToAppear` 9332 to body
fragments. The former now includes its permission array and two callback
levels; the latter keeps outer `this` separate from the middle callback's
local styled-string cell. ProgressBar callback 10110 becomes a non-executable
template with four capture insertions. Method 9832 advances from creation PC
197 to PC 401 rejection. Totals: 36 IR, six proof, 25 AST and 17 printer
rejections, 103 body fragments and 32 templates. Fragments still require the
owning ABI/module/global scope; this is not whole-record completion or proof
that every nested callable used only the generic writer.

Both CLI targets compiled and source export still produces only CommonConstants
and Logger as complete records. No tests or RSS tracking were run. Deferred
regressions cover multi-level forwarding, ancestor/middle cell separation,
repeated sibling targets, direct/mutual creation cycles, exception unwinding,
depth/scratch/work exhaustion and failure after partial child emission.

## Connected Private Object Components

Private-object evidence no longer expires every older allocation when any
tracked object escapes. Each same-block buffered allocation belongs to a
union-by-rank component. Verified own-field stores connect tracked container
and value identities; any other non-copy use conservatively publishes every
object in that component. Components are never split after overwrites.
Aliases retain identity even after publication, so storing an escaped value
cannot accidentally regain private status. Unknown uses, getters and calls
remain conservative; unrelated private register values remain private.

This admits resource construction while a separate options object remains
unpublished. It does not relax the printer's existing-key/writable-data-field
proof, nor promote arbitrary assignment to DefineField. Component roots cost
O(log I) per reference with union by rank, and preparation/storage are O(I+V)
for instruction and SSA value counts. Instruction-indexed component storage
is charged alongside the existing AST scratch before allocation; no per-object
whole-method scans or graph traversal are introduced.

ABC-only inspection in `build/pasteboard-semantic-object-components.jsonl`
advances methods 5795, 5988, 7255, 7998 and 11194 from own-field AST rejection
to capture templates. Methods 7173, 7916 and 11139 move from child creation PC
18 to PC 50, whose descendants need cross-block argument regions. Totals are
36 IR, six proof, 20 AST and 17 printer rejections, 103 body fragments and
37 non-executable templates. No newly complete record is claimed.

Both CLI targets compiled. Tests and RSS tracking remain paused. Deferred
regressions include unrelated publication, shared descendants, reverse
containment, alias use after escape, repeated stores/overwrites, block/phi
boundaries, resource exhaustion and large connected components.

## Cross-Block Argument Regions

Receiver calls can now own one cross-block argument region per method. The
existing SSA checks still prove the saved callee's exclusive use and matching
receiver identity. A new typed interval certificate additionally checks the
entire normal CFG once: no external edge may enter the interval after its
first block, every internal edge must advance within the interval, and no
internal block can terminate without reaching the call block. Returns, loops,
escaping branches and a second cross-block call region remain rejected.
This is O(B+E) per admitted method, not a whole-graph scan per call.

The unified printer emits the getter before a native receiver call, retains
its Reference across an argument IIFE, and prints the original argument
statements under forward branch labels. The outer region traversal skips
those consumed statements and their terminators. No synthetic bytecode,
reanalysis through a specialized body writer or JS-text parsing is involved.
Same-block nested calls remain supported. Original instruction PCs retain
their spans inside the argument IIFE; getter/call PCs cover the complete call.
Full child-internal PC propagation across callable wrappers remains separate.

ABC-only inspection in `build/pasteboard-semantic-argument-regions.jsonl`
moves 12 methods past AST rejection. Method 7366 passes its conditional width
argument at PC 135 and next fails on child creation at PC 219. ShareDitect
`initialRender` 11499 now emits a body fragment with nested UI callbacks;
11111 and 11139 emit capture templates. Totals are 36 IR, six proof, eight
AST and 21 printer rejections, 104 body fragments and 44 templates. Complete
record export remains CommonConstants and Logger only. Neither source-style
recovery nor whole-record semantic equivalence is claimed by these counts.

Both CLI targets compiled; only ABC output and original-PC spans were
inspected. Tests and RSS tracking remain paused. Deferred cases include
getter/call side effects, both branch outcomes, external middle entry,
early return, unsupported backedges, join-block tails, nested same-block
calls, empty block slices, quota failures and multiple cross-block regions.

## Builder Initialization and Record Integration

Local-cell initialization now uses the existing normal Lengauer-Tarjan
dominator implementation for forward-only CFGs. An allocation and initial
store must still share a block; its dominator subtree can certify subsequent
loads or first-created child captures in later blocks. Every lexical store
must independently pass SSA Language verification. The old same-block rule
remains for other graphs; backedges do not acquire a new initialization
premise. Any preceding function creation since allocation still blocks this
proof, so unknown closure mutations are not silently assumed safe.

Certificates retain per-cell initial-store/subtree bounds and block entry
numbers, not strings or copied source. Dominance is built once within the
remaining scratch budget, its work is charged, and capture queries reuse the
certificate. No per-cell CFG walk is introduced. The temporary dominator tree
dies before publishing the local plan.

The ordinary created-function entry now supplies shared bounded expansion to
its generic body. The callable wrapper also admits its already-validated
neutral-default signature form to that body. Neutral formals preserve the
ABC function length; the original IR still performs the actual default-value
assignment and branching. Rest certification and profile checks are unchanged.

`build/pasteboard-semantic-builder-integration.jsonl` admits five Builders
(7449, 8192, 9386, 10693, 11526) as body fragments. Totals are 36 IR, one proof,
eight AST and 21 printer rejections, 109 fragments and 44 templates. In actual
record generation, PageA, PageB, PasteBoard and ShareDitect advance from entry
PC 6 function rejection to PC 26 value rejection in their framework setup.
CommonConstants and Logger remain the only complete source records. Thus the
Builder integration is exercised, but the framework prelude and class gates
are not complete.

Both tools compiled and ABC-only method/record output was inspected. Tests and
RSS tracking remain paused. Deferred regressions include default-parameter
branches, non-dominating stores, repeated environment allocation/backedges,
internal-value stores, closure-before-load refusal, first-capture binding,
neutral-default callable length and budget exhaustion.

## Framework Prelude After Builder Export

Record emission now attempts the existing guarded-global-call proof at the
first string-key candidate, once per record, instead of hard-coding the index
immediately after three entry ABI moves. A Builder definition/export may
precede the framework prelude. Its exact instruction/operand matching, edge
checks and transient register liveness requirements are unchanged. The
whole-method proof is never retried at every string, so this does not add
quadratic matching. Multiple guarded preludes remain unsupported.

ABC-only record generation in `build/pasteboard-source-shifted-prelude`
advances PageA and PageB from PC 26 value refusal to PC 194 class refusal;
PasteBoard reaches its PC 182 initializer refusal. ShareDitect passes its
class emission and reaches PC 196 function creation. That target, 11027,
contains `LDGLOBAL` at PC 6 before probing a route-registration property.
The immutable realm-object load cannot simply become a mutable `globalThis`
lookup or a free identifier: global lexical bindings/property access can
differ. This remains an explicit semantic gap, not a reason to drop optional
framework registration. A verified realm-object source binding contract is
needed before this record can be published.

Both tools compiled. No new complete record is claimed; method survey counts
are unchanged by this record-only integration. Tests and RSS tracking remain
paused. Deferred regressions cover prefixed Builder exports, first-candidate
refusal, later duplicate candidates, external entry edges and live transient
registers.

## No-Await Async Bodies and Original PCs

The existing no-await completion-wrapper proof now supplies an optional typed
normalized-instruction-to-original-PC vector. Its storage shares the method
normalization budget. Async function, async method and async arrow owners
retain that vector through synchronous emission and admit the normalized body
to the generic path. Created descendants construct their own method context;
they cannot inherit a parent's PC map.

Semantic instructions retain normalized CFG positions separately from original
PC provenance. AST statements, fused getter/spill/reload positions and fused
guard positions take the original provenance before printing. This is not
reconstruction from JS text. The normalized body still has dense PCs for the
existing CFG validator. Removed promise-wrapper operations are represented by
native async callable semantics, not separate statement spans; wrapper-level
and descendant-internal generated mapping remains incomplete.

The proof continues to require the exact single catch/reject completion tail,
private promise register, straight-line no-await body and verified callable
kind. Method 7421 has real await/suspend/resume instructions and is deliberately
not admitted through this transformation. General suspension and exceptional
regions still need their own IR/control-flow recovery.

Both CLI tools compiled. ABC-only record export at
`build/pasteboard-source-async-trunk` has unchanged complete-record statuses
and rejection sites. This integration is not claimed as new corpus coverage
or runtime equivalence evidence; the detached raw-method survey does not yet
normalize async wrappers. Tests and RSS tracking remain paused. Deferred
checks include all three callable forms, promise fulfillment/rejection,
original-PC fusion after normalization, nested method provenance isolation,
resource exhaustion, actual-await refusal and malformed completion tails.

## Typed Filename Evidence Boundary

Filename consensus is now available through `ReadSourceIdentity`, returning a
typed missing/unique/conflicting/incomplete result or a structured failure.
The metadata writer consumes that API rather than owning its semantics.
Record method ranges and owner identity are checked before reading debug
payloads. Allocation failures remain typed. A future path planner can consume
the same evidence directly without reparsing JSON or scanning generated text.

ABC-only export at `build/pasteboard-source-identity-api` preserves all 19
previous consensus results: ten unique application filenames and nine missing
metadata filenames. Descriptor-based output paths intentionally remain
unchanged. The locally available es2panda code accepts `sourceFile` as compiler
input, while the release manifest points to the absent
`developtools_ace_ets2bundle` repository for the upstream build tool. The
four-part pipe-delimited identity's generating contract has not been verified;
no `.ts` to `.ets` conversion or prefix interpretation is justified yet.

The structure CLI compiled. No tests or RSS tracking were run. Deferred cases
include malformed member ranges, owner mismatches, conflicting names,
incomplete optional debug metadata and quota exhaustion. Path-format profile
verification and feeding accepted paths into collision-safe planning remain
unfinished, not replaced by this metadata API.

## Package-Relative Source Paths

The official release-manifest repository `developtools_ace_ets2bundle` is now
checked out at `OpenHarmony-v7.0-Release` and pinned in the reference lock:
commit `bfbbe397cd50e552ceadcbed3ef2a26a216afad8`. Its
`compiler/src/fast_build/ark_compiler/generate_sourcemap.ts::genKey` constructs
`entryPackage|owningPackage|packageVersion|modulePath`;
`module/module_mode.ts::addModuleInfoItem` passes that key as `sourceFile` to
the compiler. `getIntermediateModuleId` explicitly changes `.ets` to `.ts`
or `.js`. This resolves the previously unverified format without reading any
application source map or sidecar source.

A typed four-field parser feeds unique filename evidence to source planning.
Same-entry-package paths are admitted with a package directory, relative
ASCII components and a recognized intermediate filename suffix. Absolute,
empty, traversal, control-character, backslash and drive-style paths are not
admitted. Unsupported/dependency/ambiguous evidence uses the descriptor
fallback. Existing percent escaping, path limits and case-folded collision
rejection apply to both origins before any file publication. The manifest
records the typed origin as a numeric boundary label. Historical filename
formats remain available through the descriptor fallback.

ABC-only export in `build/pasteboard-source-package-paths` plans all ten
application records under `sources/entry/src/main/ets/`. CommonConstants and
Logger are actually emitted there; the eight incomplete records only receive
planned paths. Nine metadata records retain descriptor paths. Generated JS
continues to use `.mjs`; the intermediate `.ts` suffix does not prove whether
the original file was TypeScript or ArkTS, nor undo filename obfuscation.

The structure CLI compiled and the reference checkout is clean at its pinned
tag/commit. Tests and RSS tracking remain paused. Deferred regressions cover
field-count errors, dependency identities, relative path safety, basename and
case collisions, Unicode fallback, malformed filename metadata and quotas.

### Coroutine operand semantics (2026-09-25)

`analysis/coroutine` now preserves VM state/value operands in typed transfer
and read operations. Transfers distinguish await, suspend, resolve and reject;
reads distinguish resume value from resume mode. Modern forms and deprecated
await/suspend/resume/mode forms validate operand counts, v8 encoding and frame
membership. Deprecated three-register settlement remains unsupported pending
historical profile treatment; the v7 interpreter ignores its middle register,
which must not be generalized to all historical runtimes.

Provenance: pinned runtime_core `isa/isa.yaml` and ets_runtime
`ecmascript/interpreter/interpreter-inl.cpp` handlers for these operations.
The parser uses constant space/time. Register effects consume the typed slots;
they do not grant CFG fallthrough, prove Language values, or erase suspension.
The generic semantic method still rejects suspension and exception bodies.

`abc_semantic --instructions` exposes these facts at their original PCs. An
ABC-only inspection of pasteboard method 7421 produced await at 100, suspend
at 102, resume value at 106, resume mode at 111, resolve at 240 and reject at
245, with state register 5 for the transfer operations. Both CLI targets were
compiled. No tests or RSS tracking were run, per the current user instruction;
malformed operand, historical layout and register-boundary regression tests
remain deferred. This increment does not increase complete record coverage
or claim executable await output. Next, prove state identity, resume-mode
rejection branches and handler ownership before constructing an await region
with original-PC provenance.

### Await/resume region proof (2026-09-25)

`analysis/await_region` matches the modern wait/suspend/resume/mode/rethrow
sequence using typed coroutine, scalar and branch lifts. It requires identical
state registers, distinct state/result/mode registers, comparison against the
pinned runtime THROW mode, and the exact false-edge continuation after rethrow.
It preserves original await, suspend, resume, mode, branch, throw and continuation
PCs and all three register identities. A method-wide indexed check refuses
branches into region interiors and try/handler boundaries splitting a region.
No register definitions are deleted or language-value facts invented.

Runtime provenance additionally includes `js_generator_object.h`'s
`GeneratorResumeMode` and `js_async_function.cpp`'s fulfilled/rejected resumption
handlers. Region admission is limited to the existing dynamic 1202/1206/1301
semantic profiles; deprecated instruction parsing alone does not establish a
historical await-region equivalence.

For I instructions, B branch targets, T try regions, H handlers and A awaits,
the planner costs O(I + (B + T + H) log(A + 1)) time and O(A) storage, with
explicit capacity admission. It does not perform one method scan per await.
The instruction inspector attaches the typed region facts to the original
await instruction; its region allocation has a separate 1 MiB cap.

Both CLI targets compiled. ABC-only inspection of pasteboard methods 7421 and
8164 found the same region: await 100, suspend 102, resume 106, mode 111,
branch 122, rethrow 126, continuation 128; state/result/mode registers 5/7/6.
No tests or RSS tracking were run. Negative shapes, aliasing, interior edges,
exception splits, profile rejection and allocation limits still need regression
tests when testing resumes.

This is a local control/operand proof, not a complete lowering certificate.
Private async-state ownership, completion-wrapper equivalence, temporary
liveness and native await IR/AST/printer integration remain necessary. Existing
source paths still refuse real suspension; complete-record coverage is unchanged.

### Semantic effects drive register SSA (2026-09-25)

The shared semantic method path now lifts instructions before SSA and lends
that immutable instruction span to both register-use indexing and renaming.
Both stages consume `effects.normal_registers`, rather than independently
re-lifting the original bytecode. Existing register-analysis callers retain
their decoded-instruction path. This removes the SSA integration obstacle for
future recovered await operations, whose source effects must differ from the
original private VM-state operands.

The new input boundary checks full instruction-count/PC alignment, fixed-read
capacity, register/frame bounds, range expansion overflow and SSA read-ordinal
capacity. It does not claim instruction semantic equivalence on behalf of the
owner. Handler/resume-edge rejection and unchanged-CFG requirements remain.
The span is borrowed only during construction and is not retained by SSA.

No duplicate effect table is allocated. Retained IR capacity is deducted before
SSA scratch admission and charged by capacity rather than size. Lifting and
effect validation consume linear work; existing sparse dominance/phi/renaming
algorithms are unchanged. Unsupported semantic instructions now fail at lifting
before SSA, so diagnostic precedence can change without granting new support.

Both CLI targets compiled. ABC-only source generation under
`build/pasteboard-source-ir-ssa` still publishes CommonConstants and Logger;
their generated files match the prior package-path output. Eight application
records remain unsupported and nine records remain metadata-only. Tests and
RSS collection remain paused. Span mismatch, invalid slots/ranges, low work
budgets and legacy/semantic SSA parity need focused regression tests later.

Register value classification still re-lifts original instructions and must
also consume semantic operations before native await lowering can be enabled.
Private state/completion proofs and await AST/printer integration remain open;
this change does not claim additional complete records or await source output.

### Semantic operations drive value flow (2026-09-25)

The generic source verifier now passes its immutable semantic instructions to
register value flow, matching the preceding SSA integration. Value rules and
lexical/module load-premise admission consume typed operations directly instead
of re-lifting their original ABC opcodes. The standalone bytecode analysis API
remains available to existing callers.

An exhaustive typed visitor covers every current semantic operation. New IR
alternatives require an explicit value rule at compile time. Scalar rules are
shared with the bytecode path, retaining impure-NaN rejection, string-add and
non-NaN-negation facts; unknown ordinary-call results, unproven lexical/module
loads and current-environment values remain conservative. This migration does
not optimistically promote unknown results to Language.

The input boundary checks instruction-count/PC alignment and consistency of
published SSA destination slots. The caller remains responsible for supplying
the same immutable semantic input that produced SSA. No copied rule table or
second solver is introduced. The additional alignment pass performs O(I log W)
work using existing binary-indexed SSA write lookups (I instructions, W writes),
and consumes the work quota; sparse value propagation is unchanged.

Both CLI targets compiled. ABC-only pasteboard generation under
`build/pasteboard-source-ir-values` matches the prior two complete record files.
The method survey remains `[0,0,36,1,0,8,0,21,109,44]`: 109 body fragments and
44 capture templates, not 153 complete executable methods. Tests and RSS
collection remain paused. Semantic/bytecode rule parity, malformed span/slot
inputs, low budgets and NaN/copy/phi propagation still need regression tests.

Native await is not enabled yet: region proofs, private async-state ownership,
completion-wrapper recovery and AST/printer support must be connected before
admitting suspension. The full pasteboard semantic/style objective remains open.

### Native await through IR, SSA, AST and printer (2026-09-25)

The existing private async-completion proof now accepts the verified modern
await regions inside its single catch/reject wrapper. All state-register uses
outside those regions remain forbidden. The normalizer retains each original
await opcode and resume-result store, removes the proven VM suspension/mode/
rethrow sequence, and supplies typed `AwaitSite` certificates at normalized PCs.
It does not invent substitute opcodes. A bounded frame-indexed set rejects any
retained read of a removed mode-register definition before overwrite; no
per-await forward scan is used. No-await historical behavior is unchanged;
actual await recovery requires the region analyzer's supported profile.

The owning native async function/method/arrow passes those certificates only
to the generic path. `AwaitValue` enters the common semantic IR with explicit
suspension, user-code, heap and throwing effects. Its SSA input/output is the
accumulator, not the discarded VM state. Value flow requires a Language input
and produces Language on successful resumption. The source AST carries the
typed node, and the unified printer emits native `await (value)`. There is no
legacy writer fallback after this transformation. Await inside a synchronous
argument-staging IIFE is refused; making that IIFE async would change jobs and
return values. The standalone non-async wrapper printer also refuses await.
Read-only review identified loss of resource failures at the mandatory generic
boundary; the owner now preserves typed allocation/work exhaustion rather than
reporting unsupported kind, including value-flow work-quota exhaustion.

Retained instructions keep their original-PC map, including await and result
store. The await IR additionally retains its suspension/resumption/rejection
region PCs. Removed wrapper/control PCs do not yet have individual generated
spans, and child-internal spans still do not survive the outer callable text
wrapper. Thus full record-level PC mapping is not claimed by this increment.

Both CLI targets compiled. ABC-only survey
`build/pasteboard-semantic-native-await-final.jsonl` reports
`[0,0,36,1,0,8,0,13,111,50]`, versus the prior 109 bodies/44 templates.
Methods 7505 and 8248 now produce body fragments; 7145, 7173, 7366, 7888, 7916
and 8109 produce capture templates. Their nested async callbacks 7421/8164
contain native await after the original `setData` call. Raw standalone survey
of those two callbacks still rejects the original exception wrapper; owner
normalization, not raw opcode admission, enables their generated child bodies.
Full record generation remains two complete records, eight unsupported app
records and nine metadata records. This is not complete pasteboard recovery.

Tests and RSS collection remain paused. Negative state escape/alias cases,
live mode temporaries, multiple awaits, rejection ordering, original-PC mapping,
staged-await refusal and resource exhaustion require regression validation when
testing resumes. Source-style recovery, other async/handler shapes and class
construction blockers remain open.

### Async branches and settlement exits (2026-09-25)

The private async normalizer now retains resume-mode reads and stores instead
of dropping their definitions and requiring straight-line temporary liveness.
Each owner-proven mode site becomes a typed scalar definition of the pinned
`CoroutineResumeMode::kNext` on successful native-await continuation. Rejection
leaves through native await before that definition. The private state cannot
be externally resumed, and the pinned await callbacks use only NEXT/THROW.
This keeps exact register values available on later branches without inventing
undefined values or a per-register graph walk. Mode PCs retain provenance.

Ordinary branches are confined to the protected source body. Every retained
early return must have an immediately preceding settlement of the same private
async state. Verified resolve operations are removed; retained returns consume
their original source value. A branch to a removed resolve is redirected to its
adjacent retained return, while a direct branch to the original return is
refused because it bypasses settlement. Other deleted PCs are not rounded to a
nearby instruction. Original-PC indexing rebases targets, signed deltas and
encoded immediate bits together, with width/range checks before CFG admission.

The origin index is now needed internally even when the caller does not request
provenance; it shares the normalization byte budget. Branch rebasing costs
O(B log I) with O(I) retained origin storage, rather than scanning all sites per
branch. Existing bounded await-region analysis remains, and the dense removed-
mode scratch array is gone. No new special-case method-name dispatch is used.

Both CLI targets compiled. ABC-only output
`build/pasteboard-semantic-async-branches-final.jsonl` reports
`[0,0,36,1,0,8,0,12,111,51]`. Method 10250 advances from print failure to a
capture template containing its async child 10361, including native await and
conditional control flow. Method 9165 supplies a further multi-await/early-exit
case but is not claimed as fully emitted; later generic gates still apply.
Tests and RSS collection remain paused. Branch-to-settlement versus branch-to-
return, signed displacement limits, loops, multiple waits, live mode uses and
rejection ordering need regression coverage when testing resumes. Full record
completion, idiomatic source recovery and complete nested PC spans remain open.

### Survey uses production async normalization (2026-09-25)

The all-record survey now calls the same `ProveAsyncCompletion` API as callable
emission before CFG/SSA, source proof, regions, AST and printing. Its public
header lives under `include/abcd/source`; there is no second normalizer or
method-name dispatch. Retained original/normalized bodies, origin indexes and
await annotations reduce the downstream byte allowance. Metadata resource
failures remain resource failures rather than frame-layout errors.

Survey schema 4 appends the `normalize` failure stage without renumbering old
stages. Normalized artifacts carry `requires_async_callable: true`; both body
fragments and capture templates explicitly carry `executable: false`. Analysis
failures are translated back to original ABC PCs, whereas printer failures
already use origins and must not be translated twice.

ABC-only output `build/pasteboard-semantic-normalized.jsonl` has stage counts
`[0,0,27,2,0,8,0,12,113,54,3]` across the same 219 methods. The five newly visible
async artifacts are bodies 5437/5465 and capture templates 7421/8164/10361.
Their capability already existed in production callable emission; this is
consistent reporting, not five newly implemented methods or complete records.
The latter three templates map their native await text to original PCs
100/100/458. Methods 9165/9468/10333 still fail normalization; method 9193 reaches
source proof and fails at original PC 318. These are the next async blockers,
not permission to bypass ownership or language-value proofs.

Both CLI targets compiled. Tests and RSS collection remain paused; regression
coverage for schema compatibility, normalized error PCs, async ownership and
resource classification is deferred. Nested child-position propagation,
eliminated-wrapper coverage and full record completion remain open.

### Preserve numeric unary facts through SSA loops (2026-09-25)

Method 9193's async wrapper normalizes, but its loop-header register read at
original PC 318 previously failed Language proof. The entry initializes the
counter to integer zero; the backedge passes it through TONUMERIC at PC 485
and INC at PC 491. Both operations were fixed Unknown definitions, poisoning
the loop phi even though their input can be proven a non-NaN Number.

The shared scalar value rule now preserves NonNaNNumber through NEG, INC,
DEC, TONUMBER and TONUMERIC. This follows the pinned v7 interpreter's numeric
fast paths: conversions preserve the number, and unary negation or addition/
subtraction of one cannot turn a non-NaN number into NaN, including infinities
and int32 boundary promotion. Arbitrary Language inputs still become Unknown;
this does not authorize coercions or arbitrary arithmetic NaN payloads. Both
sparse SSA propagation and the legacy linear lexical evaluator consume the
same typed rule. Existing bounded monotone worklist complexity is unchanged.

Both CLI targets compiled. ABC-only survey
`build/pasteboard-semantic-numeric-unary.jsonl` reports
`[0,0,27,1,1,8,0,12,113,54,3]`. Method 9193 now passes value proof and reaches
the region-cycle gate. Body/template counts do not increase: general loop
regions still need implementation. A debugger inspection separately confirmed
that method 9165 is rejected by the single-try/single-handler ownership gate;
its nested exception structure must not be discarded as an async wrapper.

Tests and RSS remain paused. Regression coverage is still required for seeded
loop phis, mixed unknown backedges, NaN inputs, infinities, signed zero and
int32 overflow in both typed-IR and legacy scalar consumers. This increment
does not claim complete async methods, records or source-style recovery.

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
