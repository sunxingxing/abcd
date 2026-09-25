# Native Object Initialization

## Shared AST Contract

FoldObjectInitialization is the final expression-changing AST pass. It annotates a buffered
SourceAssignment with a typed ObjectField rather than matching property names,
source text or corpus identities. This first vertical slice admits one existing
data-property initialization with EmptyArray; it is not a general multi-field
or nested-object recovery claim. Other field values and shapes remain explicit.

The proof requires normal SSA, no catches, and the approved V2 exclusion of VM
resource-exhaustion differences. Within one block it checks:

- The original buffer allocation still targets the accumulator with unmerged
  origins, and its exact SSA result is consumed only by the next register save.
- That save is an unchanged pure accumulator-to-register copy, including its
  original destination and normal completion. Its register commit is retargeted
  to the literal producer, with the removed copy retaining a previous_origin anchor.
- Until the field write, only already-eliminated pure slot copies and exactly
  one eliminated, normally completing EmptyArray allocation occur. No emitted
  read, call, getter, store, branch or publication is crossed.
- The SourceOwnStore retains the same original kind/name/object and buffer pool
  identity. It reads the exact saved object and array SSA definitions; the
  array's only use is this field write, including phi consumers.

Consequently, neither the initial field value nor the private object can be
observed before initialization. The array allocation is moved into the original
literal, still once per object evaluation. The buffer writer only admits
primitive initial values and unique keys. It binary-searches the existing key
and substitutes at that original position, preserving enumeration order and
ordinary data descriptors. Computed __proto__ spelling remains mandatory.
This does not fold arbitrary Set operations or weaken own-property provenance.

ObjectField currently emits only EmptyArray and rejects other typed values.
The annotation stays on the assignment, so this pass must remain after generic
expression/return wrappers unless those are extended to carry it. The subsequent
FoldArgumentExpressions only annotates assignment results and preserves ObjectField.
Exactly one certified own-store receiver read is deducted from the saved SSA
definition's use count; all later and phi uses remain. The accumulator producer's
count remains unchanged as evidence for its retargeted-copy certificate. Ordered
native arguments may omit the register commit only when the call is that saved
definition's sole remaining use. Other uses keep the register assignment.
It introduces no hidden register reads to the exact-slot audit.

## Positions and Resources

Removed array creation and own-store nodes carry literal_origin, an original
InstructionId referencing the object producer. The common printer checks the
distance and PC, then shares the actual emitted literal span. This is distinct
from class assembly provenance and never reconstructed from JS text.

The pass scans blocks and disjoint candidate gaps in O(I), with normal SSA
lookups adding O(I log(I + U)) worst-case work for U reads. It has constant
scratch. New optional AST fields are charged through existing sizeof/capacity
admission. Existing object-buffer key sorting/parsing is reused, with one
O(log K) key lookup for K fields. The temporary field-name string is bounded by
remaining buffer scratch and dies before value-string emission. No dependency
is added. Resource failures from pool/string parsing and output limits retain
typed resource errors through the printer, rather than becoming constants or
unsupported-source errors.

## Production Evidence

Production abc_semantic/abc_structure builds and ABC-only export
`build/pasteboard-object-init-01` succeeded. Across all six pages, 102 explicit
field initialization statements merge into native literals such as
`{id: ..., type: ..., params: [], bundleName: ..., moduleName: ...}`. Source
totals change from 5,787 lines / 108,131 bytes to 5,685 / 106,157.

Home method 6016 PCs 24, 30 and 34 now all map to bytes [19938,20047), the emitted
object assignment containing params: []. Intermediate copy anchors remain.
Partial maps retain 195 methods / 9,745 spans. Statuses remain eight incomplete,
two host constructors refused at PC 14 and nine metadata-only records.

Read-only review found a resource-error classification issue, which was fixed
and re-reviewed. Tests, JS execution and RSS remain paused. Deferred regressions
include missing keys, extra object/array observations, live saves, crossed
effects, mismatched original identities, multiple fields, resource-profile
fallback, hostile strings, output limits and fused map origins. Complete source
equivalence, general object recovery and whole-process memory compliance remain
unverified.

## Native Argument Integration

Production build and ABC-only export `build/pasteboard-object-args-01` succeeded.
The 102 initialized literals now commit directly to their saved register rather
than through an accumulator copy. Of these, 37 are emitted directly in native
call argument positions instead of comma sequences. This corpus still retains
their register commits under the conservative SSA use census; it does not prove
the sole-call-use elimination branch. Output totals are 5,509 lines / 104,535
bytes, down 176 lines / 1,622 bytes, not an increase in method coverage.

Home method 6016 PCs 24, 28, 30 and 34 share bytes [19660,19766), the literal
argument assignment in Button.createWithLabel. The save PC now joins the
allocation and own-store origins. Partial maps retain 195 methods / 9,745 spans,
with the same incomplete, refused and metadata-only statuses. Read-only review
reported no concrete new defects. Tests, JS execution and RSS remain paused;
deferred checks additionally include sole-call-use removal, retained phi uses,
extra receiver reads and multi-use object commits.

## Remaining Object Copies

After initialization recovery, FoldObjectCopies retargets remaining buffered
object allocations to their adjacent register save. Original and AST buffer
identities and save operands must match. The exact accumulator SSA definition
has that save as its only use, including phi consumers. Both statements have
unmerged origins, and the save is a pure normal copy in the same block.

Allocation/evaluation/failure stay at the original PC and the register commits
only on success. No initialization is folded here: later own-field writes keep
their order and verified buffer provenance. Annotated object assignments and
catches remain excluded. The eliminated save has previous_origin pointing at
the producer. Existing ordered argument admission can then consume the literal
without a register commit only when its independent sole-use proof succeeds.
This pass changes destinations, not expression shape, and introduces no resource
failure assumption. It is O(I log(I + U)) with constant scratch for I instructions
and U indexed SSA reads. No dependency, textual dispatch or sample identity is
introduced.

Production builds and ABC-only export `build/pasteboard-object-copies-01`
succeeded. Across six pages, 46 buffered-object accumulator transports disappear;
subsequent field writes stay explicit. Source totals become 5,463 lines / 103,718
bytes, down 46 lines / 595 bytes. This does not yet recover every plain object
argument or multi-field initialization, and is not additional method coverage.

PageA method 7255 creation PC 154 and save PC 158 share bytes [5605,5640),
`$r8 = ({right: null, left: null});` including its newline. Partial maps retain
195 methods / 9,745 spans. Statuses remain eight incomplete, two refused host
constructors at PC 14 and nine metadata-only records. Read-only review found no
concrete defect. Tests, JS execution and RSS remain paused. Deferred checks
include extra accumulator uses, pool mismatch, subsequent own-field writes,
throwing allocation, catch/phi observations and already-annotated objects.
