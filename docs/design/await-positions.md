# Await Protocol Positions

`FindAwaitRegions` now preserves every original PC in its exact fourteen-step
protocol schema, including the retained continuation load. The fixed array is
optional so absence is distinguishable from a real PC zero. The matcher fills
it only after all protocol checks pass; a static assertion couples its size to
the admitted schema. Its storage is part of `sizeof(AwaitRegion)`, so existing
normalization, IR and AST capacity accounting includes the additional bytes.

The provenance travels through `AwaitSite`, `AwaitValue` and the common source
assignment. It is not reconstructed from instruction widths, opcode names or
generated JavaScript. Source admission for a normalized await now requires
this provenance when its final positions are produced.

After all ordinary/fused source spans have been emitted, `AppendAwaitPositions`
indexes the ordered AST await certificates and locates their emitted spans.
The await, result save, resume-mode definition/save and continuation keep their
own existing instruction spans. Each removed protocol instruction gets the
await expression span with typed `SpanOrigin::kAwaitProtocol`. This means a
semantic association to the native await, not a claim that a separate source
instruction executes at that position. JSON boundaries serialize `origin_kind`
as `instruction` or `await_protocol`; no engine dispatch uses these strings.

Appending after normal emission is important: existing fused span distances
are counted in original AST statements. Interleaving aliases would invalidate
those distances. Capture binding relocates both endpoints and preserves the
origin kind. Consumers must not assume spans are sorted by PC or generated
position; aliases are appended in protocol order.

With I AST statements, S ordinary spans and A awaits, indexing takes
O(I + S log(A+1) + A) time and O(A) scratch. The protocol has fixed size.
Admission charges retained text/spans, capture storage, index scratch, and
simultaneous old/new span capacities during reserve. Missing provenance,
non-monotone/overlapping certificates, duplicate/missing await anchors and
resource exhaustion cause typed refusal. No external dependency was added.

## Current Evidence and Limits

Actual pasteboard entry 8831 still builds async child 9165's shared template.
It now has 398 spans: 362 existing instruction spans and 36 protocol aliases
for its four awaits. The first alias at original PC125 maps to the native await
at PC123; the final alias at PC727 maps to the fourth await at PC701. Captures
remain template coordinates, not final file coordinates.

This does not cover the seven removed instructions of the outer async promise
wrapper, nor propagate all nested method positions into their final record
file. Those require callable-wrapper and nested-emission provenance; assigning
them arbitrarily to body spans would be misleading. The full source-position
goal remains incomplete.

Both production CLI targets build; read-only review found no concrete defect.
No runtime tests, JS execution or RSS measurement were performed. Deferred
coverage includes multiple awaits, malformed/missing certificates, template
capture insertion, non-PC emission order, fused-span distances, array-contained
awaits, duplicate anchors and low-memory reserve boundaries.
