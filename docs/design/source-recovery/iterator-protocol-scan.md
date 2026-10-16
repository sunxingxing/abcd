# Pre-control iterator evidence: current implementation boundary

Round 57's approved design separates raw protocol discovery, independently
verified region ownership, control projection, evaluation, and publication.
This working-tree increment implements **only the key resolver and candidate
facts**, not all of increment A. It grants no region ownership and has no
production call site in `PhasedBody::Build`.

## Implemented facts

`ResolveIteratorKeys` resolves named-read pool slots without TreeConstants or
source-name planning. It preserves pool/entity provenance, deduplicates pool
slots and aliased string entities, validates the entire MUTF-8 payload, and
classifies the four ASCII protocol keys at the file boundary. Other valid
strings remain `IteratorKey::kOther`.

`ScanIteratorProtocols` consumes an unchanged phased import and its original
ExceptionFlow/SSA input. It independently checks import correspondence before
using shared copy/phi SCC identities. It records acquisitions, a unique
candidate next-property definition, and actual return-read dispatch regions.
The nearest enclosing handler is computed once in parent-first region order.
An enclosing-handler association is not a claim that it is the loop's body
protection region.

`VerifyIteratorCandidates` checks the complete acquisition and region-occurrence
inventory directly against the input, including absence/duplication, the actual
original Before identity, working/original locations, canonical iterator, next
definition and region parent. It rejects claims that the currently missing
completion/guard/activation obligations are verified. It does not call the
candidate builder and does not produce `VerifiedProtocolRegionOwnership`.

The `kIdentity` fact means the acquired SSA value and its original definition
are identified. Dynamic activation is a separate, currently missing obligation.
Similarly, observing one next read is not proof of once-per-acquisition execution
or a cached, closed next-use set. All close-priority obligations remain missing.

## Complexity and accounting

Dimensions are operations, values, operand uses, original edges, regions, and
decoded key bytes. Shared import verification and SCC analysis precede linear
inventory passes. Sorting in key resolution is O(U log U); there is no per-loop
graph walk or repeated ancestor walk. The nested region relation uses parent
IDs, not duplicated ancestor member sets.

PMR scratch and result reservations are admitted before allocation. Raw string
decoding receives half the available scratch allowance; the other half covers
overlapping geometric growth. The recorded bound includes both buffers and the
decoder's strict capacity boundary. External verifier grants are conservatively
observed as a temporary peak, not as retained storage. Only the result vector
capacities survive the scan; `allocated_bytes` is a peak upper bound, not their
retained size.

## Still required before increment A is complete

- Closed iterator/next uses, activation, loop/advance and done/value order facts.
- Close entries with pending kind, payload and target; saved original/close
  exception values; guard update order; return-result checks; nested completion
  propagation and complete original-edge correspondence.
- Independent `VerifyProtocolRegionOwnership` which derives those obligations
  from the original flow and can mint an ownership token only after all hold.
- Pre-control production integration and additional typed diagnostics on both
  manifest paths and `abc_semantic`, without altering existing first causes.
- The full completion-priority matrix, ownership mutations and protocol-scale
  tests, and the requested per-method ownership report.

The current real-es2abc fixture tests candidate facts, budget boundaries and
candidate mutations. Its README explicitly distinguishes that coverage from
close-priority proof or native for-of source validation. It must not be used as
evidence that the approved ownership verifier or production integration exists.


## Increment C: identity/use index (partial, not production admission)

`IndexForOfIdentities` builds a standalone `ForOfIdentityIndex` from the original
phased import and sealed close ownership. It rechecks exception-control
correspondence, uses SCC copy/phi identities, and indexes acquisition, cached
next, advance, result object check, done, decision and value occurrences. It
checks closed iterator/next/result uses and exceptional dominance of the
successful commits. Ordinary uses of the extracted key remain ordinary; the
key is not identified with the internal next-result object. Close members stay
separate from ordinary protected body operations.

The index is **not** `ForOfPlan`, `ForOfEvents`, or permission to skip any
operation in evaluation. Single-advance execution per logical iteration,
activation/binding commit correspondence (including exhaustion), and a common
protocol/ordinary evaluation disposition are still required. No production
caller consumes the index; `kProtocolEvaluationPending` therefore remains.
Moving that gate to tree correspondence before those obligations are complete
would mislabel an incomplete evaluation proof.

Scratch uses prepaid PMR storage. Surviving capacities from delegated entry
analysis stay charged while the index is built; released delegated scratch is
not retained a second time. Dense indexes avoid per-protocol scans of all
phases or close entries. The verifier reconstructs the index from immutable
inputs and compares every member and use (it is a consistency verifier, not the
future independent event/advance/binding verifier).

The focused test uses the real synchronous close fixture, copy/phi identity
from its import, padding sizes 200/201/400/800, altered index identities and
membership, disabled acquisition contracts, and exact work/byte boundaries.
This does not replace the required Increment C dynamic protocol mutations or
end-to-end production gate acceptance.

The subsequent standalone advance/key-definition slice is documented in
[for-of-iteration-proof.md](for-of-iteration-proof.md). It still grants no
production evaluation or source admission.
