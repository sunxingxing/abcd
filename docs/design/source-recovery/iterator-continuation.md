# Verified iterator exhaustion continuation

This is a lexical control-placement correction under `language_source_v10`.
It uses the existing synchronous for-of acquisition and iterator-close proofs;
no source premise or hand-written close operation is added. Headerless/acyclic
iteration is a separate, deferred mechanism.

## Activation and ownership

The legacy route runs first. Its unchanged AST ownership census must refuse
with a typed `TreeCensusError::kOrphanExpression`, retaining expression, origin
and original operation. The source control owner then derives an
`IteratorContinuationProof` from the sealed `CloseIterationBoundary` and the
legacy natural-loop plan. It must show that the exhausted edge's original
continuation was absorbed into that same loop while a different body-exit
corridor was selected as the continuation. An unrelated census or an uncovered
protocol keeps its refusal. No dispatch uses diagnostic text, source names,
printed source or PCs.

`ExtendLoopExitCorridors` accepts verified edge constraints. The exhausted
edge's target remains outside the native loop; private abrupt body corridors
can still belong to it. This changes lexical ownership only. Physical edges,
SSA values, exception ports, pending-completion payloads and close ownership
remain unchanged. The done edge performs zero close events; an early return
performs exactly the already certified close chain at its initiating action.

## Certificate and retry view

The certificate retains protocol/natural-loop identity, original exhausted
`CompletionEdge`, original/projected targets, old and retained continuations,
body-exit cuts and pending payloads, original body coverage, protection parents
and dispatch, and original phi-copy occurrence/edge/source/destination/ports.
Copy scheduling remains the shared evaluation/exception-copy owner's job.

One owned `PhasedBody` retry holds the new control and its certificate. All
subsequent control, evaluation, syntax, ABI, capture and semantic consumers use
that view. The failed builder returns its input syntax arenas; synthesized
names/text/captures are removed before the retry. Evaluation and the tree are
rebuilt together, including deferred cell marks. Successful legacy routes do
not allocate this retry or change their projection.

## Independent verification

- Exception/control verification replays legacy control and derives the full
  proof again from original phases and sealed close boundaries.
- Close ownership and completion placement retain their existing checks;
  only the proven exhaustion constraint enters the shared corridor planner.
- Evaluation, operands and origins independently recheck the certificate and
  original phase/use correspondence.
- Tree flow retains the strict ownership census. Iterator payload identity is
  checked after phi, catch and snapshot assignments, including in this retry.
- OriginalFlow retains close coverage, cuts and protection facts independently
  of the placement certificate. RecoveryFlow compares them, rechecks original
  edges/copies and enclosing iterator identity, and checks the actual native
  exhaustion/return routes. Missing or changed certificates are refusals.

`ForOfPlacementError` distinguishes input, activation, continuation, coverage,
payload, protection, copy placement and correspondence failures, plus resource
and work limits. Failed retries preserve the original census witness/cause.

## Cost and evidence

Shared raw-phase, lexical-owner, loop-header and source-edge indexes avoid a
walk per iterator. Sparse adjacency and sorted coverage give
O((N+E+U+K) log(N+K+1)) time and O(N+E+U+K) storage, including retained certificate
output. Every owned vector/scratch allocation is charged before allocation.
Legacy products, failed syntax and retry products remain charged at peak;
pathological inputs refuse with typed resource/work errors.

The fixture is `tests/fixtures/isa/iterator_continuation`, with a source test,
Node differential, certificate mutation negatives and coherent
register/copy/block variations. The geometric scaling test includes 200/201
and scales N with fixed K; it does not claim coverage of arbitrary dense
protocol products. End-to-end and byte-stability evidence is recorded in
`build/iter-continuation/acceptance.md` and the status entry.
