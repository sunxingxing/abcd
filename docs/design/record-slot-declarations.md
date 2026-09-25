# Record Slot Declarations

## Contract

The common record printer now uses a typed census of actually emitted VM slot
reads and writes to select its private declarations. This removes unused VM
slots, not source globals, lexical cells, exports, or class fields. It does not
rename registers or claim that the remaining registers are source variables.

`CanObserveRecordSlots` exhaustively checks the AST's alternatives before the
printer enables the census. Its record-only mode admits class/function and
captured expressions in addition to the bound-read audit's existing nodes.
Nested callables own their VM slots; their outer captures are typed lexical
cells. Unknown class heritage reads a caller register through an identifier
adapter, so the printer explicitly observes that parent. The module export
writer consumes the entry accumulator through its own text boundary, so that
read is explicitly observed too. Absent heritage does not use a parent slot.

This does not widen `CanElideEntryCopies`: creation consumers are still excluded
from ABI-copy substitution. Record printing has no this/formal/rest substitution
and does not remove assignments. Statement alternatives outside the audited set
retain the previous full declaration prologue; they are not treated as unused.

## Assembly and Positions

The printer records the body insertion position after imports/export aliases,
then emits lexical declarations and the body once while collecting slot uses.
Only after successful body emission and complete export initialization does
`InsertRecordDeclarations` insert the selected `let` declarations. They precede
all body execution and lexical declarations, preserving initialization order.
The exported artifact is not exposed until this construction is complete.

Declaration selection uses enums and register indices. The insertion position
comes from the printer cursor, never a text search or a sidecar map. All entry
and descendant method spans belong to the body; each endpoint gains exactly the
inserted byte and line counts. Complete newline-terminated declarations leave
columns unchanged. Non-body or unresolved-capture endpoints reject insertion.

## Bounds and Alternatives

For B source bytes, I AST nodes, R registers and S mapped spans, the added work
is O(I + R + B + S). Slot observation is constant work per printed scalar input;
argument inputs are already part of the output dimension. Extra storage is
O(R + D), for slot-use bits and D declaration bytes, with at most one text
replacement allocation. The old text, entry spans, declaration buffer and
replacement capacity are admitted together. Method-map and slot-use capacities
were already deducted from the passed printer allowance. Resource failure
does not silently fall back after mutating the export plan.

A second body-printing pass was rejected because export state is mutable and
nested decompilation would be duplicated. Text-based reference counting was
rejected because it cannot distinguish scopes or recover semantics reliably.
This implementation reuses project slot types and standard C++ containers;
there are no new dependencies or external algorithms to license or pin.

## Production Evidence

Production `abc_semantic` and `abc_structure` builds succeeded. ABC-only export
`build/pasteboard-record-slots-01` contains 7,956 lines / 137,068 bytes versus
8,035 / 137,808 in `build/pasteboard-class-template-02`. All eight published
source records lose unused top-level declarations, 79 in total. Logger keeps
only `$acc`, `$r5` and `$r6` at record scope; method-local behavior is unchanged.

Statuses remain eight incomplete records, two constructors refused at PC 14,
and nine metadata records. Partial maps retain 195 methods / 9,745 spans.
Logger method 4658 PCs 12/14 map to [271,279), `$import0`, and PCs 22/70 to
[271,332), the full debug call. These bytes were inspected in the output file.

Independent read-only review found no concrete correctness issue. Tests,
generated JS execution and RSS tracking remain paused. Deferred regressions
include parent-only reads, export-only accumulator use, lexical captures,
staged argument stores, no-slot records, conservative unhandled alternatives,
zero-width PC anchors and tight budgets. Full equivalence and process peak
memory compliance remain unverified.
