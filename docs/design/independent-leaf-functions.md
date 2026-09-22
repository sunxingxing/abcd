# Independent Leaf Functions

Historical first increment. `initialized-captures.md` extends capture support,
renames the creation facts and supersedes the preparation/budget order below.

Permit ordinary function creation at its original straight-line parent position
only after the target successfully passes WriteCreatedFunction's leaf-only
source checks. That proves no external lexical read, outer environment walk,
home-object operation, nested creation or unsupported instruction is hidden in
the callee. The child remains a native function expression; every dynamic
evaluation creates a new object and its own prototype. Never hoist/deduplicate
function values by target method.

Extend LinearLexicals with sorted unique IndependentCreation facts keyed by
instruction offset and a LocalFunction statement. Facts are caller-proven and
bound to the unchanged body and target source context; non-creation/method or
unused facts are rejected. Merge facts while visiting instructions in O(I+C),
not a per-instruction scan. Default empty facts preserve existing refusal.

WriteLocalLexicalBody optionally receives a shared MethodLookup together with
MethodConstants. It verifies each child into a discard sink, supplies facts,
then emits LocalFunction as an accumulator assignment at that position. Child
source failures become typed parent creation/resource/work failures at the
creating PC; callers needing the detailed callee diagnostic can call
WriteCreatedFunction directly. BaseFunction forwards the optional lookup.
WriteCreatedFunction itself remains leaf-only, so this adds exactly one layer.

Prepare children before parent analysis. Resolve C creation sites, sort typed
target-position/length keys, verify each of U unique targets once, and render
each into an exactly sized owned byte array. This is an output artifact cache,
never a semantic string representation or runtime object cache. Pool aliases
resolve to the same method key. Source offsets/facts remain in instruction
order. The sum of repeated fragment sizes is checked against the output budget
with division before multiplication.

Budget admission includes candidate/fact/site/fragment arrays, cached source
bytes and child decoding/name/analysis storage. Temporary candidate storage is
released before parent graph/environment/plan analysis; retained cache bytes
are subtracted before that analysis. Constants and external inputs remain
caller-admitted. Work is O(C log C + C(log regions + log methods)) plus a
constant number of each unique child's passes, parent analysis, and emitted
bytes. Repeated sites never re-scan the child's non-emitting instructions.
No exponential recursive preflight. Sharing is per parent preparation, not a
cross-parent/file cache; that remains a function-task service responsibility.
Parent plan work and each child plan work remain individually
bounded; global function-task work/cancellation is not implemented here.

Validate with failing then passing C++/Node tests: creation after internal acc,
parent local cell surviving creation, no extra reads/side effects, child called
with primitive/object values, distinct identities across sites and parent calls,
constructor behavior, captured/nested/invalid child refusal, fact mismatch and
exact/short budget/output. Run existing regressions and sanitizer checks. This
does not publish records or claim full closure recovery.
