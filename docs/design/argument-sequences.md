# Straight-Line Argument Sequences

SourceStagedCall already proves that a property Reference must be retained
across argument evaluation. Its printer previously wrapped every argument
interval in a synchronous arrow IIFE. For a straight-line interval consisting
only of SourceAssignment and NoOperation, it now uses a parenthesized comma
expression in the first argument. This reuses the assignment grammar already
used by native array-element emission; it is not a textual JS rewrite.

Property lookup remains before every staged assignment. Assignments execute
in original order, then the first argument register is read, then subsequent
argument registers are read. All exceptions stop evaluation at the same source
operation. No value is duplicated, hoisted or discarded. The final register
value prevents the comma expression from changing the first argument. The
removed arrow contributes no declarations or explicit this/arguments binding.

Branch argument regions, nested staged calls, local/module stores and other
statement kinds retain the previous region/IIFE writer. Call-depth admission
and the prohibition on await within staged calls are unchanged. Existing
creation, rest, global and buffer expression checks still apply. This change
does not authorize additional bytecode or claim native async recovery.

The call index is checked against the AST size, and the first index must not
exceed it before constructing iterators. Each successful straight-line scan
and emission is O(A) for A statements and uses O(1) extra storage. Rejected
outer intervals stop at a nested staged node rather than scanning that node's
child interval. Statement emission continues to own original PC spans and
fused-origin relocation; no positions are reconstructed from generated text.

## Evidence

Both production targets, abc_semantic and abc_structure, built successfully.
The ABC-only pasteboard export in `build/pasteboard-argument-sequences-01`
contains 8,727 lines / 150,224 bytes, compared with 8,938 / 154,233 in
`build/pasteboard-expression-slots-01`. The 211 removed wrapper lines include
all four Logger logging methods. Coverage remains eight incomplete source
records, two refused constructors at PC 14, and nine metadata records.
Partial method maps retain 195 method entities and 9,745 spans.

Independent read-only review found no concrete correctness issue. Tests,
generated JS execution and RSS checks remain paused. Deferred regressions:
property getter before argument effects, throwing intermediate assignment,
empty/NoOp-only intervals, reference receiver identity, nested calls and branch
fallback, invalid bounds, await refusal, and map coordinates after wrapper
removal. Export counts are not behavioral-equivalence evidence.
