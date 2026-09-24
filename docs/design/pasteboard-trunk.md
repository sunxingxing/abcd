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
