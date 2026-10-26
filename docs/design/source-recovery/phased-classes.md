# Classes in phased creators, increment 1

`ClassCreator` borrows either the actual normal file function or the actual
phased file function. It never converts exception SSA into normal SSA. Normal
creators delegate to their existing inventories and preparation paths.

The phased body retains one heritage inventory and a sorted original-PC to
working-instruction index. `PhasedClassPoint` connects each live class operation
to its Before node, input environment, result Commit node and selected exception
target. Original PCs select immutable constructor/member file indexes; working
PCs identify evaluation, capture and exception events after async/generator
normalization. Neither coordinate system substitutes for the other.

The family reader projects class barriers separately from ordinary phased
function bindings. The existing Discover/Verify/Prepare/Bind/Finish creator-class
pipeline prepares constructors, buffer members and descendant classes. Class
roots use `ClassCaptureSite` and `ExceptionScopeFacts` at the real Before node;
the existing capture planner and target verifier bind them to the creator's
actual cell storage. No synthetic function creation or normal capture proof is
introduced.

Only successful binding of all prepared roots can mint `PhasedClassBinding`.
The expression forest and operand verifier require its actual occurrence,
revision, environment and Before/Commit/exception correspondence. Scope
independence alone grants no phased admission. Finishing independently checks
the correspondence, capture/enclosure destination and owner ranges. The fresh
exception-tree verifier checks the composed class at its original execution
point, including result availability only after Commit and the creation's
exception edge. `VerifyFamilyWrites` consumes every phased class barrier and
rechecks the binding, point and root capture obligations. Printing retains
working PCs for capture plans and original PCs for member identity.

The increment adds no source-language premise. Existing source TDZ, module,
resource-failure and exception contracts remain binding. Ordinary phased
functions remain on their separate existing path. No member installation or
initializer is fused across exception ports. Descendant heritage still refuses
with `FamilyCreationCheck::kClassHeritage`; deferred declarations and unavailable
capture storage retain their existing typed refusals. Computed/static member
forms not admitted by the shared path remain refused.

For the added bridge, N counts original/value operations and values, E exception
edges, U uses/capture obligations, and K class occurrences/member roots across
the actual prepared family occurrences. Heritage/copy facts are shared once;
source sites are sorted once per creator; queries are logarithmic; phase points
are constant-time queries. Added work is O((N+E+U+K) log(N+K)), with linear retained
indexes and the existing independently budgeted member analyses. Byte admission
precedes allocation; capacities and actual sort comparisons are charged. Typed
resource/work refusals apply. The inventory test uses 200/201/400/800 compiled
class sites and counts the actual value inputs, including register-allocation
copies. This is not full-process RSS or cohort evidence.

Regression evidence lives in `tests/fixtures/isa/phased_classes/`, its ISA Python
producer, the 28-case Node differential, and `phased_classes_test`. Mutations
cover site/PC/environment/Before/Commit/original correspondence, missing bound
evidence, whole-family write checking and byte/work exhaustion. Node compares
original and emitted modules; only ABC is supplied to the engine.

Review correction: the discarded `({});` at the start of these creators came
from an unused private Hole token, not from NEWLEXENV. Environment pushes were
already classified as checked environment events. Phased Hole admission used
to allocate a source token whenever any Hole protocol was present, including
absent class heritage. The shared Hole proof now records whether non-native phi
transport needs storage. With no phi transport, comparisons or rethrows, the
verified pure Hole/copy transport to absent heritage has the normal route's
anchor disposition and needs no source token. Real comparisons/rethrows and phi
storage retain their existing representation. Fresh evaluation verification
rebuilds this fact; the tree verifier still checks token declarations and every
environment/capture operation. There is no printer text filtering or new premise.
