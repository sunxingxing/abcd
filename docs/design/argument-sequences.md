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

## Ordered Native Arguments

`HasOrderedArguments` now recognizes straight-line intervals with one assignment
per argument, in call order, interspersed only with NoOperation anchors. Targets
must exactly match the call operands and have strictly increasing register IDs.
The increasing-ID restriction is conservative: it proves distinct targets in
O(A) time and O(1) storage without sorting or pairwise comparison. Duplicate,
reversed, missing, extra and non-register targets fall back to the sequence.

The initial audit admitted scalar, member and string expressions. These have
no hidden writes to caller-private register bindings. Since later targets are
distinct, immediately capturing each assignment's result as its argument equals
reading that target after all assignments. Getter effects, assignment order,
property Reference and receiver identity are unchanged. Assignments remain in
the output, preserving subsequent uses and exception-visible state; removing
them requires a separate SSA use proof and is not part of this transformation.

StatementForm distinguishes ordinary statements, sequence items and native
arguments. The argument mode omits only the terminator, not the target or its
slot observation. Every child still passes through Statement, including NoOp
anchors and fused-origin relocation. No new retained allocation is introduced.

Production builds and ABC-only export `build/pasteboard-ordered-arguments-01`
succeeded. Output totals are 8,420 lines / 146,977 bytes versus 8,457 /
147,149 previously. Logger's four log methods now have native argument lists
with retained assignments. Record statuses and partial map counts remain eight
incomplete, two refused at PC 14, nine metadata; 195 methods / 9,745 spans.
Read-only review found no concrete issue. Tests, JS execution and RSS checks
remain paused. Deferred cases additionally include duplicate/out-of-order
arguments, later reads of retained targets, getters observing exception state,
empty intervals and PC coordinates across adjacent argument separators.

## Broader Value Audit

HasArgumentValue now exhaustively admits comparisons, module/global/realm
reads, local/captured reads, ordinary/member/constructor calls and empty or
primitive-buffer object/array creation. These expressions can have heap effects
or throw, but cannot rewrite caller-private VM argument bindings. They remain
evaluated in the same order. Buffer admission, capture tokens, module scope and
host-contract checks still belong to their existing typed writers. Calls are
not treated as pure.

Anonymous function/class definitions remain excluded: omitting an assignment
target can affect inferred names. Method/accessor creation, rest, await and
super also retain separate admission protocols. The ordered-target check and
SSA use proof are unchanged; admitting an expression kind does not by itself
permit removing a live register commit. New SourceExpression alternatives
require an explicit compile-time audit decision.

Production builds and ABC-only export `build/pasteboard-argument-values-01`
succeeded. Five page records now emit updateStateVarsOfChildByElmtId with direct
elmtId and fresh empty-object arguments. Totals are 8,041 lines / 137,914 bytes,
down from 8,056 / 138,039. Statuses remain eight incomplete, two refused at PC
14 and nine metadata; partial maps remain 195 methods / 9,745 spans.
Independent read-only review found no concrete issue. Tests, generated JS and
RSS remain paused. Deferred regressions additionally include fresh allocation
identity, throwing calls/constructors, capture mutation without VM-slot aliasing,
global/module binding validation and anonymous-definition fallback.
