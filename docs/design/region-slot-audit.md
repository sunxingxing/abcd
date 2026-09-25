# Region Slot Audit

## Array Owners

HasObservedReads now admits SourceArrayCapture, SourceArrayCommit and
SourceArrayLiteral. Their actual VM inputs/targets all pass through the same
bound-read/slot-observation paths as ordinary statements:

- ArrayCapture observes the accumulator through Slot and writes a distinct
  generated element binding, not an untracked VM register.
- ArrayCommit observes its last capture and writes its register via Target.
- ArrayLiteral writes its destination via Target, prints every element's
  assignment interval via Statement in sequence mode, and reads its final
  accumulator through Slot. Its delayed private array save already requires
  the existing non-observation proof; the audit does not widen that proof.

SourceIndexedStore anchors may be consumed by the validated array owner. A
standalone indexed store still rejects in the printer. Admitting its node to
the read audit does not authorize a new property-store lowering or publish a
partially unsupported body. The flat audit continues through all child
expressions instead of skipping an array interval. Sequence-mode copies are
not elided; their actual assignment targets remain observed.

## Branch Arguments

An argument region no longer automatically disables the audit. ArgumentRegion
itself emits labels/control syntax and delegates each block to Statements in
argument mode. Branch predicates, assignments, nested calls and the final
argument values use the same Predicate/Statement/Value paths. The complete flat
AST remains audited. Its no-parameter arrow IIFE introduces no own this,
arguments, formal or rest binding that could shadow substituted owner inputs.

FindEntryBindings still requires its whole-SSA unique-definition/read proof;
uncertain joins do not gain aliases. Region topology, scope-depth admission,
evaluation order, catch handling, and parameter staging are unchanged. Catch
regions initially retained conservative declarations; their separate census is
described below. Unaudited operations remain conservative. This is
slot census and certified copy omission, not branch or array reordering.

## Cost and Evidence

The change adds no retained storage or traversal beyond the existing linear
flat AST audit. Slot-use and entry-binding storage keep their existing capacity
admission. No new dependency or external algorithm is introduced. Original PC
span recording remains on each statement/owner, including zero-width anchors.

Production abc_semantic/abc_structure builds succeeded for both steps. The
array-only ABC export `build/pasteboard-array-slots-01` did not change source
sizes relative to `build/pasteboard-register-receivers-01`; this is not claimed
as observed array coverage improvement. After admitting branch argument
regions, `build/pasteboard-region-slots-01` contains 6,315 lines / 117,615 bytes
versus 6,471 / 119,355. All six pages improve, while both common-record sizes
remain unchanged. Home's branch-bearing Button callbacks now omit unused VM
declarations and self wrappers without changing their argument-region labels.

Statuses remain eight incomplete records, two constructors refused at PC 14,
and nine metadata records. Partial maps retain 195 methods / 9,745 spans.
Independent read-only reviews found no concrete correctness issues. Tests,
generated JS execution and RSS checks remain paused. Deferred regressions
include element-only slot use, array-target-only use, aliasing/partial arrays,
standalone indexed-store refusal, branch-dependent argument registers, formal
and rest substitutions, phi joins and generated positions. Full source
equivalence and process memory-cap compliance remain unverified.

## Exception Body Census

CanObserveBodySlots separates the exhaustive emitted-slot audit from
CanElideEntryCopies. RegionTry writes the caught value through Target, then
delegates both protected and handler bodies to Region. The lexical `$caught`
binding is not a VM register. The same flat audit checks all child expressions;
unsupported owner protocols still prevent precise declarations.

PrepareEntryBindings performs the census before checking for a normal semantic
method. Exception owners call the NormalFlow body/template overloads without
that method pointer, so putting the census after that check leaves the feature
disconnected. No normal-SSA entry substitution or rest-local transport is
enabled for these overloads or catch regions. Assignments, caught accumulator
installation, handler order and exception checks are retained. Only unused
declarations and self wrappers certified by the existing downstream consumers
are omitted. PC spans still include installation at the first handler span.

Slot tracking remains O(operands), with capacity-admitted O(registers) storage.
The additional flat audit on normal bodies is linear; no per-register graph
walk or new dependency is introduced. The approved execution contract and
resource-exhaustion exclusion are unchanged.

Production builds succeeded. The initial `pasteboard-catch-slots-01` export
was unchanged and exposed the missing overload wiring. After correction,
`build/pasteboard-catch-slots-02` contains 6,258 lines / 116,713 bytes, versus
6,315 / 117,615 previously. Home, PasteBoard and ProgressBar account for the
57 removed lines. In particular, Home and PasteBoard no longer need class
self wrappers for onPageShow/checkPermissionGrant and handlerPasteHtmlData;
the two affected async click callbacks omit their unused arrow self wrappers.
Business statements and catch structure are unchanged in the inspected diffs.

Statuses remain eight incomplete, two refused constructors at PC 14, and nine
metadata-only records; partial maps retain 195 methods / 9,745 spans. The
read-only review found and then confirmed correction of the overload wiring.
Tests, JS execution and RSS remain paused. Deferred regressions include slots
used only by handlers, caught-accumulator-only writes, nested catches, captured
handler cells, live self registers, rest/formal transport retained under phased
SSA, map rebasing, tight memory budgets and conservative unsupported nodes.
These export observations do not establish full source equivalence.
