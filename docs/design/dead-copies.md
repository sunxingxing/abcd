# Dead Assignment Elimination

## Contract

The common AST pass applies standard SSA use-count worklist DCE, restricted to
normal private accumulator/register copies and primitive loads. This is an in-tree implementation,
not a new dependency or a general effect optimizer. It runs only after all
source input checks, region construction and receiver-call fusion succeed.
Original IR and SSA are unchanged. A removed statement becomes a NoOperation
at the same original PC, yielding a zero-width generated anchor.

Counts include every original operand read and every phi input, including
exception handlers. Each still-emitted copy whose sole SSA output has zero
uses is queued; removing it decrements its sole input count and can enqueue
that input's defining copy. Counts are exact rather than saturated. This
handles copy chains without repeated method scans. Definitions enter at most
once as their count reaches zero. Phi cycles are conservatively retained.

Only original slot copies or primitive loads still represented by
SourceAssignment are eligible. Primitive loads are typed SpecialValue,
IntegerLiteral or Float64Bits, not arrays, strings, objects or conversions.
Their AST must also retain a primitive scalar expression. Both normal and
before-throw language effects must be empty and exceptional completion must
be impossible; all fused/special-origin statements are excluded. A dead
primitive load has zero SSA reads and therefore decrements no dependencies.
Removing a copy can enqueue its now-unused primitive producer using the same
bounded worklist. No additional allocation or new graph traversal is needed.
Calls, property accesses, allocation, coercion, local stores, captured cells,
control transfers and other effects are not candidates. In particular, a
previously fused NoOperation does not decrement counts: its replacement may
still consume the original values at a different generated position. This
conservative boundary avoids reinterpreting source text or reconstructing
uses from printed expressions. Input checks are not weakened; this pass cannot
make an internal or unknown catch value source-admissible.

## Cost And Evidence

With V SSA values and C removed assignments, elimination costs O(V+C) on phased
SSA. The ordinary facade still binary-searches its R native reads, so that
path costs O(V+C log R). The bounded queue has at most I instruction IDs;
its storage is admitted beside all live AST, use counts and existing proof
scratch. Exact size_t counts replace the previous one-byte saturated counts;
their larger storage is reflected in AST admission. This trades a bounded
increase in scratch for avoiding repeated liveness scans. Count collection
and existing AST construction retain their separate costs.

Both CLI targets compiled. ABC-only inspection of pasteboard 6044 in
`build/pasteboard-6044-dead-copies.jsonl` reduces template text from 2368 to
2236 ASCII bytes while retaining 103 parent PC spans and two capture
insertions. Unused ABI copies and argument scratch copies disappear; the
template remains conditional and non-executable. The ordinary all-record
survey retains stage counts `[0,0,27,1,0,8,0,11,113,56,3]`, and the source
manifest at that historical checkpoint was unchanged: only CommonConstants
and Logger were reported as complete records.
Read-only review found no concrete defect. Tests and RSS remain paused.

When execution checks resume, cover copy chains, multiple dead consumers,
handler phi uses, loop-carried values, fused/staged calls, zero-width spans,
200/201 scaling and byte limits. Current production inspection is not a
regression-test or runtime-equivalence claim.

## Primitive Load Extension

Both production CLIs build, and read-only review found no concrete defect.
ABC-only export to `build/pasteboard-dead-primitives-01` retains eight incomplete
application records and the two host constructor refusals at PC14. Compared
with `pasteboard-local-assignments-01`, combined source changes from 10,443
lines / 181,677 bytes to 10,263 lines / 178,667 bytes. Logger's four logging
methods retain their calls and returns but no longer write dead undefined
values to acc first. These counts measure output, not equivalence or runtime
performance. Tests, generated JS execution and RSS remain paused. Deferred
regressions additionally include dead constants behind copy chains, constants
live through handler phis, special numeric values, and effectful expressions
whose results are unused but whose evaluation must remain.
