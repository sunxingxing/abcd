# Conditional Values From If Regions

ConditionalValue generalizes the Boolean diamond plan instead of adding a
parallel body writer. It contains the shared private target, the instruction
IDs of both arm expressions, and optional opposite-Boolean polarity. The old
Boolean optimization remains a specialization of the same region proof.

ValueArm retains single-entry RegionIf ownership, one normal successor to the
same join, exactly one stored assignment per arm, no initialization metadata,
and no merged-origin nodes. Admitted operands are typed scalar values, strings,
local cells and captured cells. Calls, composed members, creation/name protocols
and intermediate expression statements remain outside this admission boundary.

For non-Boolean operands, the shared printer writes a conditional expression
whose true arm is the original fallthrough arm. Only the selected operand is
evaluated; cell initialization/domain checks remain active, and failure happens
before the target commits. Operand emission uses each original arm instruction
PC. This is not eager evaluation or substitution based on source text. Target
and Expression continue through the existing slot and binding census.

Each arm assignment maps to the actual generated operand span, while the header
and removed join jumps map to the complete assignment. Existing Boolean cases
retain their whole-assignment maps. No AST/source string is copied into a second
model. Proof and map emission inspect each owned arm a constant number of times,
with constant scratch and the previous linear region cost and memory bound.

Tests, JS execution and RSS remain paused. Deferred regressions include TDZ and
capture-domain failures in selected/unselected arms, target/input aliases,
non-Boolean primitive results, empty-array allocation selection, string grammar,
same Boolean arms, extra effects, nested origin rejection and PC diagnostics.

## Production Evidence

Both production tools built successfully. ABC-only export to
build/pasteboard-conditional-values-20260925 succeeded. Five page Builders now
use native conditional values for receiver selection. PageA method 7449 emits
`$acc = $acc ? $inner7449_0_0 : $inner7449_0_1;`. Predicate PCs 35/38 map to
[686,733), body PC 40 to the operand [700,714), and alternate PC 44 to its
operand [717,731). Original Boolean diamond output is retained.

Totals become 4,076 lines / 88,862 bytes, down 20 lines / 110 bytes from
pasteboard-if-arm-order-20260925. Maps retain 195 methods / 9,745 spans.
Statuses remain eight incomplete, two host constructors refused at PC 14 and
nine metadata-only records. Read-only review found no concrete defect. Tests,
generated JS execution and RSS checks were not run; complete source structure
and semantic equivalence remain unfinished.

## Member Operands and Argument Regions

MemberExpression is now admitted as an operand without treating a getter or
Proxy access as pure. Only the selected arm performs the binding/property
evaluation, once, and it still commits the target after successful evaluation.
An annotated receiver must originate inside that same arm, precede the member,
and name this exact member instruction. At most one previous_origin NoOperation
is allowed, and it must match that receiver's certified adjacent source guard.
Other fused, class, literal and previous-origin combinations remain refused.

ConditionalArm emits the arm's original prefix anchors before its expression,
allowing BoundReceiver to relocate source/guard spans using the same instruction
coordinates as ordinary arm emission. The expression receives its actual RHS
span, and the removed join jump remains a zero-width anchor. The header and its
fused origins now map to the emitted condition prefix, before the arms. This
supersedes the earlier whole-assignment mapping for non-Boolean headers/jumps.
Boolean diamond maps remain unchanged.

The same admission now applies inside a ProveArgumentIf-owned IIFE. It changes
only that region's two-arm assignment, not the outer Reference or argument
commit/return protocol; elimination of the wrapper remains separate work.
Prefix anchors and expressions are each visited once, preserving linear arm
work and constant scratch. Deferred cases additionally include throwing getters,
unselected missing modules, receiver/guard coordinates outside the arm, nested
argument ownership and exact module-guard source positions.

Production builds and ABC-only export
build/pasteboard-conditional-members-20260925 succeeded. Eleven width arguments
across the six pages now use a conditional member expression inside their
existing IIFE. Totals become 4,032 lines / 88,620 bytes, down 44 lines / 242
bytes from pasteboard-conditional-values-20260925. PageA method 7366 PCs 110
and 112 (module source/guard) map to [6002,6010), `$import1`; member PC 120 maps
to [6002,6023), `$import1.FULL_PERCENT`; alternate PC 126 maps to the actual
numeric operand [6026,6031). The join jump remains a zero-width anchor.

Partial maps retain 195 methods / 9,745 spans and record statuses remain eight
incomplete, two host constructors refused at PC 14 and nine metadata-only.
Read-only review found no concrete defect. Tests, generated JS execution and
RSS remain paused. Native outer conditional-argument emission and whole-program
equivalence remain unfinished.
