# Immutable Entry Bindings

The common printer now shares one entry-binding proof for receiver and fixed
formal copies. It replaces the single-candidate receiver-copy implementation;
there is no separate per-parameter path or graph traversal.

The callable owner lends its existing complete fixed-parameter may-write counts
through ScalarNames. Zero proves that the ABI slot retains the formal's entry
value. Unknown accesses, rest parameters and neutral-default forms do not
provide this certificate. The vector remains alive across synchronous print,
preflight and constructor fallback. Native receiver authority remains the
separate typed this-binding certificate; parameter authority does not authorize
arrow/derived receiver substitution.

Body-only printer APIs intentionally use an empty wrapper FrameLayout. The
certificate therefore carries the actual first-parameter register, and the
binding table uses the supplied register-name span length. The formal window
is checked against that length before indexing. Initial ABC-only export caught
a regression caused by using the empty wrapper frame; the corrected transport
was reviewed through the full callable-to-printer call chain.

An O(R) table stores each candidate's source register and original SSA
definition. Candidates come only from the pure entry register-copy prefix and
must read independently immutable input slots. One shared scan of SSA values
rejects other definitions and phis; one shared scan of SSA reads rejects
initialization-before-use. An overwritten candidate cannot regain validity:
every original definition still participates in that shared scan.

Building the table is O(I + V + U + R), not O(parameters * method size).
Scalar operand substitution is O(1) per operand with fixed expression scratch.
The actual table capacity is charged before any output/locals/span allocation.
Resource exhaustion is reported rather than silently abandoning the proof.

Read expressions use the immutable ABI input, allowing existing parameter names
and native this bindings to flow through member reads, stores and predicates.
Assignment targets still use original register identities. Identifier-only
captures, class-parent references, declarations and initialization remain intact.
Catch/phased methods are outside this initial proof. This does not yet remove
all transport statements or recover mutable source variables.

Read-only review found no concrete correctness issue. Tests, generated JS
execution and RSS measurement remain paused. Deferred checks include mutable
inputs, multiple writes to candidate destinations, phis, initialization-before-
use, declaration/capture retention, constructor fallback lifetimes and 200/201
plus geometric scaling. No complete source-equivalence claim is made.

Both production targets built successfully after the transport correction.
ABC-only export `build/pasteboard-entry-bindings-02` retains native receiver
reads and now emits `currentBreakpoint` directly in GetValue comparisons and
`sm`, `md`, `lg` directly in constructor field assignments. Eight incomplete
app records, two constructor refusals at PC14 and nine metadata records remain.
Method maps retain 195 method entities / 9,745 spans. This verifies generated
output changes, not runtime equivalence or full mapping coverage.
