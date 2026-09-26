# Literal Initialization

This implements the first proof component of T07, not permission to replace an
arbitrary own-definition with assignment. The working IR must already have
input/effect correspondence; fresh normal-flow verification additionally checks
use-chain integrity and operand availability for every planning/check invocation.

## Initial Domain

LiteralPlan groups all live ValueOwnWrite operations by their exact allocation
OpId. The receiver must be the direct result of a live empty ordinary object
allocation, in the same basic block, earlier than every grouped definition.
Copies/phis are not guessed to be aliases: existing proved copy rewriting may
resolve them before this pass; unresolved aliases are explicit refusals.

Every definition retains the original operation, key and RHS occurrence. No
deduplication by pool index or property spelling occurs. Duplicate names must
keep definition order. Both admitted own-store and DefineField operations on
these fresh ordinary objects produce writable/enumerable/configurable data
properties. Pinned v7 provenance: RuntimeStOwnByName and RuntimeDefineField in
`ecmascript/stubs/runtime_stubs-inl.h`; own-store's class non-enumerability branch
does not apply to a freshly allocated ordinary object. No upstream source is
copied or new runtime dependency added.

All RHS values must already be available at allocation: literals, language
entry/phi values, or dominating operation results. A same-block result produced
after allocation refuses with kLateValue. A language-domain check does not
itself make an ABI entry printable; subsequent binding/ABI checks still apply.
Allocation uses other than the group's exact receiver occurrences must follow
the last definition. This includes copies, property reads, calls, writes and
using the object as a stored value. Cross-block uses and phi incoming uses occur
after the allocation block under fresh flow proof; same-block definitions cannot
be bypassed by a normal branch. Exceptional handlers are not introduced by this
normal-flow plan. OOM differences remain excluded only by the established source
contract, not by changing decompiler memory limits.

## Independent Check and Cost

Planning collects D definitions, sorts (allocation, definition) once, and forms
flat contiguous ranges. Verification checks supplied ranges, unique allocations,
increasing definition order, exact receivers, RHS availability and complete
one-time coverage of all live own-definitions. It then follows each allocation
value's use chain once to reject early observation. It does not regenerate or
compare against a new candidate plan. Distinct checked allocations have disjoint
result-use lists; no per-object graph scan, ancestor walk or transitive alias
closure is performed.

Beyond the shared normal-flow checker, planning costs O(O+U+D log D), checking
O(O+U+D), and additional storage O(O+D), for O operations and U uses. All scans,
sort comparisons and capacities share the invocation's remaining work/byte
grant; checker invocation does not reset the allowance. Reported retained bytes
are not whole-process peak RSS. Tests for 200/201/geometric growth and exact work
limits are written but uncompiled/unrun during the testing pause.

## Source Integration

Source rules 18 connect this proof to evaluation, the owned AST and printing:

1. A typed property arena and object expression use computed string keys
   for data definitions, including `__proto__`; never colon-style prototype
   setters, ambient Object.defineProperty, or normal [[Set]] as a substitute.
2. Evaluation pins RHS snapshots that dominate allocation. Do not inline/move a getter,
   call, closure creation, or function-name inference into a property initializer
   merely because the property's raw ValueId is available.
3. Evaluation/effect verification uses an explicit initializer placement and
   freshly verifies the supplied LiteralPlan before relocating definitions.
4. Independent TreeFlow/operand checks verify actual property order, keys,
   RHS identity and one allocation, consuming the same freshly checked plan.
5. Allocation expressions and individual property nodes carry original sites
   to separate printer position events. No generated-text provenance inference.
   Success still requires the frozen tree and publication checks to close.

The property arena has unique syntax ownership; missing, repeated or orphaned
properties refuse. Definition OpIds map to PropertyIds, not fabricated statement
positions. The CFG, SSA values, block ownership and scope identities are unchanged.
Source regression specifications cover duplicate `__proto__` keys, 200/201
properties, reordered origins, shared syntax and forbidden RHS inlining. They
remain uncompiled/unrun under the testing pause; integration is not behavioral
or real-corpus acceptance.

Buffered objects, arrays with holes/length, computed keys, later-produced RHS,
cross-block initialization, escaped receivers, prototype/descriptor mutation,
and closure captures remain separate required extensions. This initial domain
does not redefine T07 or the corpus-wide goal around easy empty-object examples.
