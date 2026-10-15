# Round 76: static initializer self-binding evidence

2026-10-15, baseline `a19178ed`. Evidence-only stop under the round's explicit
rule: no admission, guard, declaration, timing, printer or budget change.
No paired source was read. All PCs below are original ABC instruction starts.

## Actual order and identity

| Record | Creator | Inner env / class creation | Inner self store | Initializer method / call | Child reads / guards | Later outer publication |
|---|---|---|---|---|---|---|
| o0/4660 RemoteDeviceModel | m4850 | env6, class13 | PC28, depth0 slot0, creator cell0 | m5040 defined40, called51 | load10 / guard12, ordinal0 Guarded | local result retained62; getInstance call78; export81 |
| o20/8166 IndicatorBarAttribute | m8392 | env32, class39 | PC54, depth0 slot0, creator cell2 | m8556 defined250, called261 | load11 / guard13 and load90 / guard92, ordinals0/2 Guarded | inner env popped271; export272 |

Both orders are **creation -> inner self store -> initializer call -> outer
publication**, not creation -> initializer call -> self store. `FindClassSelfStores`
independently finds these candidates using the actual cell access groups and
EffectOrder (src/source/class_self.cc:69-108): a unique parent store, dominance
and the private allocation/creation/store effect interval. This does not yet
prove absence of descendant writes or native class-name equivalence.

The explicit constructor operand of m5040's `newobjrange` at18 is the guarded
self read; its result becomes an own static field via `stownbyname` at22.
The structural source shape is `static <field> = new <self>()`.
m8556 constructs the self class at81 and169, storing own fields at85 and173.
Its arguments also read imported values, global properties, and two outer
captured functions. These are real, potentially throwing/re-entrant events.
The creator's cleanup handlers are H58 and H268 respectively; their protected
ranges include the initializer invocation and rethrow after popping the inner
environment. The child methods have no handlers.

m8556 is not a self-only capture case. At creator entry env0, function8447 is
stored to depth0 slot0 at19 and function8364 to depth0 slot1 at30. After the
new class env32, those are child depth1 slot0 (`backgroundBar`, read19) and
slot1 (`thinStrip`, read98). The self capture is child depth0 slot0. The names
come from the ABC environment literal, not from reference source.

A standalone diagnostic probe (outside the repository, linked against the
unchanged libraries) reads the initializer in conditional mode using the actual
CreationTreeContext and module context, and queries the creator's NormalScopeFacts.
It does not grant a source receipt or mutate the production reader. Results:

| Child | Capture obligations in original order | Rebase | ClosedClassFields | ClassSelfOrder |
|---|---|---|---|---|
| m5040 | Guarded(0,0) | local, no deferred store | kUnsupported(0) | kOwner(0) |
| m8556 | Guarded(0,0), Language(1,0), Guarded(0,0), Language(1,1) | all local, no deferred store | kUnsupported(0) | kOwner(0) |

Thus the self cell is already initialized at this child creation. No new source
TDZ premise is needed for these occurrences. A different occurrence before its
store would still require the original throwing guard; this evidence grants no
such occurrence initialization and says nothing about unrelated cells.

## Why changing the reader alone is unsound

1. `ReadClassInitializerFunction` selects Closed at class_initializer_file.cc:40-41.
   This explains the current first refusal at each child's HoleGuard:
   `SemanticProofError::kInternalValue(3) > InternalValueReason(6)`, m5040@12 /
   m8556@13, under both flag sets.
2. `ClassSelfOrder::Build` requires an empty capture set at class_self_order.cc:75
   and a ClosedClassFields certificate at76. The latter allows closed RHS own
   definitions, but rejects other observable/throwing events at
   closed_class_fields.cc:53-59. It cannot certify these constructors, imported
   property reads or re-entrant calls. The conditional probe confirms both
   refusals without changing production admission.
3. The initializer tree is built with IncomingCells::kReject and no incoming
   CaptureTree at class_tree.cc:456-464. It is not a prepared member family
   carrying the creator's cell proof.
4. The native static-field print destination rejects captures and self, and all
   non-module symbols at tree_printer.cc:203-208. Removing these checks would
   not prove which lexical binding a printed class name denotes.
5. Existing class naming is a shadowing-avoidance proof, not an inner-cell
   binding proof: class_names.cc:236-263 follows captures back to their real
   owner and disables names that would hide it. tree_printer.cc:954-961 and
   2247-2259 deliberately preserve NamedEvaluation without introducing an
   immutable inner binding. class_initializer_projection.cc:148-155 retains
   the original self-cell write; it does not remap initializer captures.

An ordinary creator capture cannot simply be printed inside the class: in
`let Outer = class Inner { static x = ... }`, Inner is initialized during static
initialization, while Outer is not. Here the ABC's private self store occurs
before the initializer, but the retained creator store in source completes only
after the native class expression. Round 43/67 deferred-creator evidence alone
does not establish this inner/outer binding correspondence. Round 38 preserves a
throwing read; it does not grant the read a different, already-initialized name.

## Required follow-up proof (not implemented)

A class-initialization scope/event proof must join the existing creator/capture
and recovery facts before the closed-only projection can be generalized:

- identify a class occurrence's inner cell separately from outer captures,
  creator symbols and class result; prove its single initialization and all
  transitive writes, including re-entry through constructor/static/member calls;
- retain the actual guard and independently discharge its original exception
  correspondence from the proven initialization, or retain a TDZ event for an
  early read; never infer binding identity from a spelling;
- bind the class-internal reference to the native inner name with explicit
  scope/activation identity; ordinary outer captures keep their own bindings;
- compose initializer evaluation, own-field commits and abrupt completion into
  the class creation event without moving outer stores, publishing the class
  early, or changing cleanup exception paths; constructor calls and initializer
  throws must retain their original order and payload;
- verify the same correspondence in the initializer, creator and final class
  enclosure, including captures retained by re-entrant descendants. The printer
  consumes that evidence instead of inferring it from expression forms.

This is a cross-layer protocol beyond the existing deferred-capture/guarded-read
machinery, so Round 76 stops before changing production code. It should not need
a new language premise if the original inner binding and all observations are
proven. For any unproved occurrence the current refusal must remain.

The follow-up needs the requested early/after-store, outer-vs-inner, re-entrancy,
initializer throw, extra write, swapped creator/cell/store and exception-edge
mutations, plus copy/register/block variants and 200/201/400/800 exact-budget
checks. A real es2abc fixture must establish both binding cells and the actual
store/call order before Node differential testing (exception constructor and
traces, not message/stack). None of those unimplemented projections is claimed
validated by this evidence-only round.

## Evidence artifacts

`/private/tmp/abcd-round76/` contains the four raw instruction listings, original
record metadata, standalone `probe.cc` / `probe.log`, build/test logs, fresh
exploration exports and the per-record comparison report. The probe uses
existing analysis APIs with their normal budgets; it is diagnostic, not a new
production algorithm. No production buffers or asymptotic costs were changed.
