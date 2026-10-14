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
