# Closed descendant class preparation (640dd19f follow-up)

This slice publishes normal descendant creations whose class member families
have no requirements on creator cells, no deferred creator declaration, and no
heritage or static-initializer projection. It introduces no source-language
premise. Classes in functions and in class methods use the same record-root
CreatorClasses / ClassPreparation / PreparedFamily pipeline.

## Preparation and ownership

ReadFunctionFamily's explicit DFS retains actual class obligations instead of
ending at ClassPreparationGate. Pool resolution still uses the actual creator
method. DescendantClasses owns a creator-indexed preparation list. An explicit
work stack discovers its class occurrences, verifies layouts, prepares member
families in requirements-only state and collects their existing bottom-up
requirements. A nonempty requirement is refused; binding is checked against
FamilyFunction::Storage, never a fabricated creator input. A real deferred
binding is reported separately before any creator AST can be constructed.

FamilyTreeInput::Prepare retains this preparation before building a creator.
Class families receive contiguous full owner ranges, including admitted classes
inside their ordinary creators. PreparedFamily::OwnerCount and the constructed
family's OwnerCount distinguish the complete syntax namespace from its ordinary
member count. Before completion is accepted, VerifyFinishedClassOwners walks
actual trees with an explicit stack and checks every owner exactly once against
the reserved half-open range. It does not trust the cached count.

Requirements-only class members that themselves contain another class retain
kClassNestedPreparation. This is an implementation boundary: the next level
needs an inter-class preparation graph. No recursive Prepare/Finish permission
is inferred from the absence of captures. The 200/201/400/800 depth fixtures
exercise ordinary-function occurrence depth, not arbitrarily nested class
completion.

ReferenceNames includes the class families' actual global/module references,
mapped to the real creator's ordinary-family subtree for name reservation. No
symbols are allocated by this proof. Existing record-root preparation order and
owner numbers are unchanged. The final namespace check includes nested classes.

## Completion and independent checks

The creator draft contains the discovered class layout at the original creation
OpId. Once the creator CaptureTree exists, FinishCreatorClasses calls the common
BuildClassTree / PreparedFamily::Finish with that enclosure, creation and real
storage. VerifyTreeSemantics then checks the completed creator again. The
independent FamilyWrites::Classes verifier indexes the actual creator member,
checks original class barriers and composed ClassExprs and marks that creator's
sites. The printer's obsolete module-root-only restriction is removed; its
existing prepared-body, actual creator and destination checks remain.

No declaration is moved or reallocated. Phased creators keep their existing
refusal. Captured, deferred, heritage, static-initializer and nested-preparation
gaps have distinct appended FamilyCreationCheck values and real locations in
both JSON paths. Existing enum values and resource classes stay unchanged.

## Complexity and budgets

New work is O(N log N) in input occurrences, operations, names and owner slots;
all new vectors, work stacks, names and owner indexes are charged before
allocation. The family reader now uses a charged std::set for its active methods
(logarithmic cycle checks), rather than allocating a whole-file method bitmap
for each separately read class member. This avoids a repeated whole-file scan
and allocation as class width grows. The allocator charges its actual rebound
node size before allocating. No memory/output/work cap is raised.

Tests measure exact accepted byte/work boundaries, not only reported costs.
Nested existing passes also have independent scratch-work admission floors;
returned cumulative cost need not equal the minimum admitted work allowance.
Width800 preparation passes its exact-boundary tests but the separate whole-
record export reaches FamilyEnclosureError::kResourceLimit under the existing
cap. Depth800 exports; Node's default parser stack is exhausted on its generated
function-expression nesting, while `node --stack_size=8192 --check` passes.
The original declaration fixture passes Node's default stack. This is recorded
as a downstream resource limitation, not a reason to change source semantics.

## Tests and acceptance

- creator_classes_test: real family discovery and production preparation at
  width/depth 200/201/400/800, exact byte/work boundaries, original pool and
  occurrence mutations, owner-range collisions and rejection of unfinished trees.
- descendant_classes_test: real production draft/completion; independent
  FamilyWrites checks; wrong creator/pool/range, class-barrier mismatch and a
  missing constructor are rejected.
- descendant_classes_cli_test: real es2abc input, both contracts, Node source
  differential for construction, [[Set]] through an Object.prototype setter,
  throwing constructor, local class in a method, repeated loop creation with
  distinct identities, an intentionally colliding `$value0` global reference,
  and equivalent copy/branch variants. [[Define]] and
  removed-throw mutations are detected.
- descendant_class_refusals_cli_test: capture / deferred / heritage / nested
  preparation remain refused with real method/PC, identical CLI/manifest causes.
- Existing family_creation_failure_cli_test now checks completed output and
  original instruction/pool/CFG evidence for the four targets. In previous flags,
  TwoWayTlsWorker instead exposes its pre-existing capture initialization gap.
  The phased o36 case remains unchanged. The JSON enum test covers all added tags.

## Target boundary

| o18 record | Creator / original PC | Previous flags | --language-source |
|---|---|---|---|
| 52728 MulticastWorker | m53236 / 179 | complete | complete |
| 59564 TcpClientWorker | m60156 / 232 | complete | complete |
| 60829 TcpServerWorker | m61449 / 126 | complete | complete |
| 63861 TwoWayTlsWorker | m64425 / 46 | capture initialization refusal | complete |

The previous-contract TwoWayTlsWorker leaf is
`CaptureCellError=3 > CaptureError=1 > ScopeError=3`, m64229, original/working
PC62, capture ordinal2, depth0/slot7, requested domain0. Its separate creation
source is m64341/PC21. The class gate is gone; no capture/TDZ rule was weakened.
The m60156 creation remains reachable through the original CFG despite its
position after a return in disassembly order.

Final acceptance results and log paths are recorded in status.md. No holdout,
full-suite or RSS benchmark is authorized or run here.

## Final verification

Baseline exports: explore67b / explore67bs. Final exports:
`/private/tmp/r67c-delivery-cohort/` (log at the same prefix `.log`).
Previous flags 516/620; source contract 556/620. No status regressions or text
drift in existing complete/partial records, including o32/49868. Only the four
target record entries change in either manifest set. All seven new complete
flag/record outputs pass `node --check`.

Full keep-going build passes. Fixed source-regression passes 65/65. New/extended
checks pass 8/8. Targeted class/capture/deferred/guarded/family/creation/recovery
passes 72/86; the 14 failures below are already registered in status.md.
CLI/corpus/failure/receipt/manifest sweep passes 60/61; only the existing
family_source_cli_test fails. Commands and individual logs are linked from
`/private/tmp/r67c-delivery-acceptance.json`.

The targeted existing failures are family_source_cli_test,
class_tree_source_test, unguarded_derived_cells_source_test,
unguarded_literal_closure_source_test, unguarded_derived_literal_receiver_source_test,
captured_receiver_source_test, unguarded_captured_receiver_source_test,
unguarded_forwarded_receiver_source_test, reversed_captured_receiver_source_test,
unguarded_reversed_captured_receiver_source_test,
unguarded_reversed_forwarded_receiver_source_test,
reversed_initialization_captured_receiver_source_test,
unguarded_reversed_initialization_captured_receiver_source_test and
unguarded_reversed_initialization_forwarded_receiver_source_test.

Existing integration expectations changed only where this increment changes the
real first result: creator_classes_cli_test now sees the located capture gate;
family_creation_failure_cli_test checks complete source for the newly admitted
classes and the next capture leaf for old-contract TwoWayTlsWorker. Original
instruction, constant-pool and CFG-reachability assertions remain in place.
