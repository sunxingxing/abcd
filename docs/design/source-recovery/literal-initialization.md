# Literal Initialization

## Native binary initializers

ValueBinary with two operands and an independently established Language result
can use the existing native BinaryExpr inside normal or phased literal ownership.
This changes expression eligibility only: arbitrary arithmetic is not promoted
to Language, and VM NaN/internal-value refusals remain input-proof failures.
Original operand order, conversions, single-use ownership, initializer intervals
and phased catch snapshots remain checked. Shared results and debugger barriers
refuse instead of duplicating or moving evaluation. The eligibility check is O(1),
with existing reverse operand ownership and independent canonical-use scans;
there is no new dependency, per-expression graph walk or physical-memory claim.
Decoded tests and independent native JS oracles cover string addition, strict
equality, relational comparison and bitwise OR, including coercion and throws.

## Initialized cell guards on phased paths

The common completed-store/conditional incoming-cell proof also identifies
nonthrowing hole guards before phased literal planning. Literal verification
rechecks CellEvaluation before excluding such guard operands from the canonical
use census and guard operations from initializer effect intervals. Placement
alone is insufficient: missing, stale or invalid initialization evidence refuses.
Original phase edges and PC origins remain available to independent tree checks;
conditional incoming evidence still requires enclosing family discharge.

Guard lookup is O(1) per operation/use after the existing indexed initialization
verification; the extra pass is O(I+U), plus one existing cell-verification cost
per literal validation. No per-guard dominator rebuild or alias walk is added.
Three-profile decoded tests cover 1/2/200/201/402 nested fields, checked exception
trees, forged witnesses and native source payload/identity behavior. This does
not establish full ProgressBar acceptance or process RSS compliance.

## Unobserved phased transport inside native literals

Phased use censuses exclude operands of an independently checked pure dead
subgraph, as well as verified aliases. `VerifyElidedPureEvaluations` checks every
proposed elision for pure effects and no exceptional completion, then checks all
uses for closure: a dead result cannot reach a retained consumer or phi/catch
copy. It is independent of the builder's backwards liveness worklist. The extra
verification costs O(operations + uses) time and O(1) scratch memory, charged to
the existing literal budget. No original graph operation or PC is removed.

Nonthrowing ordered allocations retain their creation/identity and operand
interval checks, but have no exceptional exit for catch comparison. Throwing
owned effects still require the enclosing allocation's handler and staged-copy
sources. Shared results, unfinished observations and changed catch snapshots
remain refusals; a forged dead label cannot hide either an effect or a retained
copy result.

Three-profile decoded fixtures cover pure copy chains and nested empty arrays
at 2/200/201/400/800 copies/properties. Serialized nested read/array functions
traverse common file/family emission, ABI, instruction sources and the printer;
elided copy PCs retain anchors. Independent complete-function behavior checks
cover getter/Proxy effects and thrown identity, fresh outer/inner objects and
arrays, prototypes and descriptors. Actual PageA 7421 now passes literal and
phased evaluation planning; its common expression/call reference construction
remains incomplete. Focused evidence is not whole-record paired acceptance.

## Native unary initializer expressions

Common literal nesting admits the shared single-input lowerings for negate,
bitwise not, logical not, typeof, ToNumber and ToBoolean. This changes expression
ownership, not numeric-domain admission: the existing input and tree proofs must
still certify a language value, including the VM NaN representation restrictions.
ToNumeric and increment/decrement now reuse the common numeric-domain and
exclusive UpdateBinding lowerings. Updates save their operand once to compiler
scratch and perform native prefix update there; they do not write the source
operand or substitute arithmetic by one. ToNumeric currently lowers identity
only for proved non-NaN Number input. Literal eligibility does not widen that
proof to arbitrary Number/BigInt coercions. VM predicates that repeat a snapshot
operand remain outside this initializer admission.

Primitive ValueCopy loads are admitted only for a typed integer, float or special
constant input with no effects or exceptional completion. ValueId copies are not
primitive leaves. Original constant/unary sites stay in the shared origin DAG.
The same unique-use census, laminar operand/property order and actual catch-copy
equivalence apply on both normal and phased paths; sharing cannot duplicate
coercion or creation. No new writer, dependency or per-variable graph walk is added.

Decoded fixtures cover all six admitted forms in three profiles, 2/200/201/400/800
unary properties, shared-result refusal, uncertified general-language NEG and
unsupported numeric updates. Serialized constant NEG and dynamic bitwise-not
fixtures traverse common file/family emission, ABI, instruction sources and
printer positions. Complete generated functions are compared with independent
native expressions for getter/Proxy/ToPrimitive order, arbitrary thrown identity,
BigInt/Symbol and numeric cases, effects, fresh objects and own descriptors.
These focused checks do not constitute full-record paired acceptance.

## Phased native initializer nesting

The same literal ownership model now admits fresh aggregate, read and call
subtrees on unique normal phase corridors. Canonical operands use the independently
checked evaluation alias map; one census over all uses rejects shared results,
unfinished aggregate observations and catch/phi snapshots through those aliases.
A supplied nesting witness is checked rather than reconstructed by the verifier.

The laminar stack retains original allocation, operand and property order. Each
folded effect and definition must have the outer allocation's exception target
and identical staged catch-copy sources. Reusing `PhaseCorridors::SameCatch`
checks actual throw bundles, not just lexical try membership. Different handler
ranges and a changed catch-visible local remain refusals. Environment/state
barriers and arbitrary late snapshots are not erased. No original SSA/phase
operation, edge, scope or PC is edited.

ExpressionForest's existing bounded dependency worklist resolves initializer
bases through those aliases. The shared origin checker likewise recognizes the
actual nested RHS, keeping separate allocation/read expressions and definition
properties. Native member references across phases use this freshly checked
nesting placement; independent CallPlan and operand checks retain the exact
receiver. No `.call`/`.apply` adapter, new writer or dependency is introduced.

Beyond existing flow verification and definition sorting, the added census and
corridor indexes cost O(operations + uses + blocks + edges + original copies)
time and space. Each compared catch bundle is charged to its own original site;
there is no per-property graph walk or nested ancestor scan. Retained plan-byte
accounting is not peak-process RSS evidence.

Actual decoded fixtures for profiles 12.0.2/12.0.6/13.0.1 exercise nested own
initializers through shared evaluation, exception AST, TreeFlow and semantic
checks at 1/2/200/201/400/800 properties. Missing/wrong consumers, reordered
properties, shared aggregates, unfinished catch observations and unequal
exception/catch snapshots refuse. Serialized file fixtures also traverse class
and family emission, callable ABI, instruction sources and the common printer.
Independent JS references cover getter/Proxy order, native receiver, arbitrary
throw identity, retained effects, fresh outer/inner objects, own descriptors,
inherited setters and exact original printer positions. These are focused
functional checks; full-record paired semantic/style acceptance remains open.

The historical normal-path extensions below describe their original delivery
scope. This section extends their shared ownership checks to phased nesting;
late arithmetic and guarded module RHSs are not added by this change.

## Late construction expressions

Normal block-local initializer nesting also admits `ValueConstruct`, using the
same typed operation and common call/expression checks as a standalone `new`.
The constructor result must have one initializer consumer; its callee and
arguments retain their exact operand order and effects. The independent use
census rejects an unfinished enclosing object passed into construction and a
construction result shared outside the initializer. Exceptions, replacement
objects, getter receivers and allocation identity are not erased. Phased
initializer admission is unchanged.

`literal_construct_test` decodes actual create/new/read/own-store bytecode and
checks evaluation, tree flow and printing at 1/2/200/201/400/800 properties.
Its malformed nesting proof is independently refused; structurally valid
escape/shared-result graphs are refused. `literal_construct_source_test`
compares complete generated functions with an independent object initializer
through 28 constructor/getter/Proxy effect and throw scenarios, object identity
and property descriptors, plus six non-constructible values. This is focused
source behavior evidence, not full-record or paired-corpus acceptance.

## Aggregate operands of late expressions

Normal block-local expression operands may now be nested fresh objects or arrays,
including primitive literal buffers and owned initialization writes. The same
unique-consumer census forbids sharing or escape. The laminar stack checks their
enclosing literal owner through the expression consumer, rather than requiring
an immediate own-write consumer. An operand's interval ends at its final owned
definition, or allocation for a buffer/empty leaf; it must finish before the
consumer and before the next operand begins. Property intervals still preserve
their original effects. Arbitrary late values and phased nesting remain outside
this extension.

ExpressionForest constructs mixed read/call and aggregate/property dependencies
with a bounded Kahn worklist. Each checked nested node has one syntax parent;
initializer consumers point to the actual allocation. Pending child counts and
one parent edge per node give O(O+N) additional time and O(O) storage for O
operations and N nested nodes. This orders AST construction, while LiteralPlan,
TreeFlow and operand checks preserve runtime order. Cycles or unresolved nodes
are typed failures. No recursive lowering, repeated scans, third-party code or
new dependencies are added; buffer-property assembly keeps its existing cost.

The decoded constructor fixture now includes `{value: arg.value}`, `[9]` and
`{}` as three arguments at 1/2/200/201/400/800 properties. Reordered arguments
and shared aggregate results refuse, including a forged LiteralPlan whose
consumer edges match the reordered graph. An additional unit fixture alternates
aggregate/property and constructor edges at 1/2/200/201/400/800 nesting depth.
The complete generated argument function passes 288 independent JS cases for
effect/throw order, replacement objects, Proxy construction, exact argument
identity, retained mutations, prototypes and descriptors, plus six checks that
arguments evaluate before a non-constructible callee throws TypeError.

Real PageA 7255 now reaches a complete tree, TreeFlow, ABI and instruction-source
checks. The enclosing family still fails in later child 7366; this remains
component evidence, not complete record publication or paired-source acceptance.

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
