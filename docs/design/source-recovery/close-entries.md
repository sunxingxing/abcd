# Original close entries — Round 79 / A3a

This is standalone analysis, exercised only by unit tests. No PhasedBody,
topology, control, evaluation or publication hook is installed.

## Inputs and derivation

`DeriveCloseEntries` accepts matching original synchronous body/ExceptionFlow/
ExceptionSsa, its unedited phased import, resolved IteratorKeys and the candidate
scan. It independently validates the scan/import before using their identity
and association facts. It accepts **no pending-completion or entry list**.
Those facts do not grant region ownership or native for-of admission.

Shared SSA copy/phi identity, full exception-CFG dominance, normal-loop and
typed boolean-dependency indexes establish:

1. The acquisition commits before the cached-next call and the close candidate.
   Next is called on that same iterator. The done read's base is that call's
   result, and the predicate implements ToBoolean on that exact done value.
   The original exhausted and success edges are retained. A body that always
   exits can have no executable backedge: its advance remains an advance, with
   an absent loop header, rather than a fabricated loop identity.
2. The return getter is guarded by private boolean state: false is established
   after acquisition and before the first advance; true writes belong to its
   close guards. The predicate takes the getter arm for false and the bypass
   for true. Its pure input prefix has no observable event. Copy aliases are
   followed; a stale state snapshot and intervening reset are refused. State
   use links must end at these predicates, not escape as user values.
   Distinct state storages are not merged merely because their boolean values
   agree; unproved transfers between them keep a typed guard refusal.
3. The return method's callee and receiver retain the acquisition identity.
   The call takes no arguments. These are data-flow facts, not an adjacent
   opcode sequence or a fixed register/PC pattern.

The pure bypass continuation supplies Return(payload), Break or outer Continue.
Return's canonical original SSA value must already be available on the entry
edge (rechecked by A2). Break targets the actual exhausted boundary derived
from the done test; outer Continue targets an original enclosing natural-loop
header. **No lexical follow choice or exit-corridor layout ownership is used.**
An empty, nonthrowing control-only path on the exhaustion edge is normalized
with its original identities retained; register reads/writes and language
effects stop this forwarding. A read-free cycle refuses, never proves progress.
Other continuations, effects on the bypass, ambiguous state/next/done identities
or an unproved boundary refuse with fixed-size typed evidence.

For Throw, the bypass must rethrow the real body-protection Catch payload.
All actual dispatch-selected edges into that Catch are enumerated; each carries
its explicit source SSA value or its original exception-exit identity. Sources
must be dominated by acquisition and the successful not-done edge. An error
from a preceding inlined close is therefore the **current** pending Throw,
not the earlier Break/Return. This slice does not supply the full nested-close
ownership/priority certificate.

Every entry retains the source edge, originating return/jump/throw, the real
Catch when present, and a transfer edge for loop control. A2 receives exactly
these derived entries. Ordinary close cuts, selected internal return-protection
exception paths and external exception exits retain original identities.

## Verification and conservative exits

The verifier rebuilds immutable indexes, discovers guards by walking dominance
ancestors backwards (the builder uses forward dominator stacks), recomputes
all entry/exit sets and reruns the raw relation. It does not trust producer
coverage, guard locations, arrivals or telemetry. Scalar truth and pure
continuation transfer definitions are shared; this is not another interpreter.

No A2 semantics changed and no unverified state seed is introduced. Original
edge-qualified phi inputs already narrow known closed-state paths. Unknown
sentinel comparisons remain conservative and are labelled
`kConservativeSentinelValues`; exhaustion is never a successful proof.

The real es2abc fixture independently checks these original boundaries:

| Method / return read | Derived source entries | Pending completion | Raw may exits |
| --- | --- | --- | --- |
| m423 @105 | 85 Commit → 87 Before | Break to original PC312, jump @155 | 7 |
| m423 @196 | 176 Commit → 178 Before | Return original value from PC61, return @248 | 7 |
| m423 @271 | @139/146/150/230/237/241 → Catch251 | Current Throw from each source | 6, one rethrow @307 per entry |
| m451 @96 | @72 → Catch76 | Throw the actual @72 input | 2 |

The seven-exit cases retain unproved Hole-comparison possibilities. They are
not full IteratorClose priority certificates and cannot authorize control
elimination. Fixture/compiler provenance stays in the iterator_protocol README.

## Resources and tests

All scratch/growth is prepaid PMR storage. Delegated analyses use exclusive
remaining grants; retained output is charged before further allocations.
Candidate verification does not expose a scratch peak, so its entire grant is
conservatively observed; `peak_bytes` is an upper bound, not measured usage.
Exact sufficient/one-byte-insufficient admission is tested separately.

Indexes are shared; loop ancestry uses intervals and sparse traversals use
generation stamps rather than clearing a node array per entry. State validation
is shared per acquisition; return calls and raw SSA reads/writes/phi arms are
indexed once (no hidden repeated SSA binary searches). Dense alias or
verification contexts consume the N log N work grant and refuse the whole
query. Admitted work is O((V+E+U) log N); no cap is raised.
N includes original graph/SSA/use/boundary items and the raw frame-slot index.

Tests cover the real entries/payloads/targets, current Throw propagation,
register permutation, exhaustion-edge block splitting, producer and verifier
work boundaries, exact bytes,
200/201/400/800 input growth, and mutations of entries, payloads, targets,
omitted entries, relation exits, initial state, state reset, receiver and done
direction, and a read-free exhaustion cycle. There is no Node equivalence or complete close-ownership claim in
this slice.
