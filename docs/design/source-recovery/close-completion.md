# Close completion diagnostic analysis (A3b, partial)

`close_completion` is standalone and has no production hook. It re-derives
`close_entries` and the original completion relation, and records original
members and edges (including exception cuts). It does not grant topology
ownership or source admission. No region, instruction or exception is removed.

The proof states are intentionally separate:

- Guard and return-reference facts reuse the independently checked A3a activation
  proof, plus the unique callee use and receiver identity from the immutable IR.
- For a Throw entry, priority requires every reported exit to retain the entire
  original Throw tuple. An exit-free cycle does not prove this obligation.
- A3c adds outcome-indexed non-Throw priority. Success/object and nullish
  outcomes must keep the entry completion; getter/call failure must propagate
  that exact close exception; an object-check failure must propagate that
  check's exception identity. Any unproved outcome or conservative sentinel
  path keeps the entry's priority obligation missing. See the A3c section below.
- Ownership records raw membership and checks external predecessor edges and
  semantic roles. It remains missing: private scalar live-outs and overlapping
  user-try exclusion have not been proved. An arbitrary object check does not
  count as a return-result check; its operand must be this call's result.
- Nested current-completion propagation has not been implemented. Multiple
  acquisitions are conservatively missing for this obligation, including
  unrelated acquisitions; this is a diagnostic coverage gap, not a restriction
  on source semantics or permission for a production path to refuse a program.

The verifier re-derives the immutable entry proof with A3a's backwards guard
algorithm and recomputes the summary; it does not consume supplied member masks
or verified flags. Priority/reference transfer definitions are shared; this is
not an independent implementation of the pinned interpreter. Telemetry is not a
certificate field. `peak_bytes` is a conservative scratch bound including
exclusive delegated grants, not measured allocations or RSS.

Every PMR allocation and surviving vector capacity is prepaid. All sparse walks
and sorting are charged to one input-sized N log N admission grant; dense
repeated-region inputs refuse work rather than admitting quadratic work.

The unit test covers the existing real m423/m451 ABC, original-payload Throw
preservation, conservative Break/Return refusal, forged flags/payload/targets,
missing/duplicated members, missing/changed edges, wrong receiver/reset/guard
inputs through A3a, register permutation and edge splitting. 200/201/400/800
prefix-size cases test exact producer byte/work admission and verifier work
boundaries. These are not dense completion/nesting scaling tests. No new Node
or runtime priority-matrix equivalence is claimed.

Remaining work: complete private ownership and user-try exclusion, nested
current-completion bridges and their fixture/mutation matrix. A3c's outcome
relation closes the absent-outcome representation gap, but does not remove
conservative raw exception/sentinel paths or grant topology ownership.


## A3c: outcome-indexed completion

`AnalyzeCloseOutcomeRelation` is a separate synchronous analysis query; the A2
API and its value/state specialization are unchanged. Both use the raw event
engine. The new specialization carries the outcome in the same interned state
as the entry, persistent SSA environment, pending payload and control target.
No independent sets of kinds, payloads or outcomes are joined.

The source analysis derives the observed return-read and call from the checked
A3a occurrence/reference evidence. The raw observer identifies getter/call
normal Commit and original exception edges. Copies and edge-qualified phis
preserve typed return-method/result provenance. Loose comparison of the method
value with undefined/null, followed by typed Boolean predicates, supplies the
nullish edge. A normal object check supplies SuccessObject only when its operand
is this call's result on that path; its exception supplies PrimitiveResult.
No adjacency, register number, same-block or exact region shape is required.

GetterThrow and CallThrow carry the actual observed operation's exception-exit
identity. Catch entry and subsequent stores/phis propagate that identity through
the existing relation; the priority table never substitutes the entry payload.
The builder observes normal Commit departure. Verification replays the immutable
original edges with the opposite worklist order and derives success on entry to
Commit, separately from the builder's event-stage transitions. Pinned scalar and
Boolean meanings are shared. Supplied outcomes and classification are not proof
inputs. The source verifier also checks every saved outcome tuple.

Every raw may-exit remains. Unknown non-protocol exceptions are Unproved with
OtherException and the real source node. An unresolved Hole comparison retains
its two successors, the observed outcome, and a SentinelPath uncertainty witness;
it is never silently resolved. For example, m423 can conservatively reach Break
after GetterThrow through this comparison: this is a missing proof, not a verified
swallowed close error. No result is returned on state/work/byte exhaustion.

The existing real es2abc fixture is sufficient: its iterator is an opaque input,
so its expanded close code contains the getter failure, call failure, nullish,
object-result and primitive-result alternatives. m423's Break and Return entries
both expose all five outcomes. Their first missing witnesses remain the raw
nullish-comparison exception edges (PC114 and PC205); their sentinel may-paths are
also explicitly recorded. No compiler-generated edge is removed to obtain a pass.

The unit test verifies that erasing outcomes yields exactly A2's original may
relation. It checks outcome swaps, original/close-error replacement, primitive
misclassification, removal of uncertainty, payload/target identity, and the
priority transfer table including outer Continue and original Throw. Pure table
tests do not mint raw certificates. Register permutation and split-edge variants
are included. 200/201/400/800 NOPs are inserted *inside the traversed close path*,
with exact producer byte/work and verifier work boundaries and state exhaustion.
All new PMR nodes, state/output capacities and retained results are prepaid.
The existing linear sparse-state cap and input-sized N log N admitted-work cap
apply; there is no resource-limit increase or production hook.
