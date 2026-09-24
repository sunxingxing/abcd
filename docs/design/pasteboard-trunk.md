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
