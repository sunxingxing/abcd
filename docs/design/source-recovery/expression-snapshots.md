# Shared expression snapshots

The common evaluation plan owns one evaluation of a shared SSA result. A
private slot preserves its identity for later uses. An eligible eager use may
receive the assignment itself, instead of a preceding standalone assignment.
Source syntax does not define eligibility: comparisons, unary expressions,
property bases/keys, writes and call arguments use the same occurrence model.

The historical `kPredicateSnapshot` placement now denotes this general eager
use-site assignment. `TemporaryReason::kPredicateSnapshot` is a different fact:
a VM predicate lowering repeats its input and therefore needs retained storage.
Do not confuse those facts or relax the latter to allow repeated getters.

## Ownership and order

The existing verified local forest supplies original definitions, live uses and
the initial `MultipleUses` root. `DescribeEvaluation` supplies expression/eager
capability. Candidate discovery selects the earliest original local occurrence;
it does not select producer opcodes, strict comparisons or branch adjacency.
The consumer must support eager input evaluation and must not require a
reference-before-RHS or repeated-input snapshot for that occurrence.

The existing forward event-interval builder consumes operands in reverse
declared order. An effectful producer and its owned operand subtree can join
the consumer only when their interval ends at the expected event boundary.
Every other effect/environment event still contributes to that boundary. An
intervening getter, call or write therefore prevents this relocation.
Event counting uses the main forest's projection domain: independently proved
cell/module/receiver guards and folded literal/class definitions contribute no
standalone event. A staged literal definition restores its original root via
`BeforeLogicalFusion` and remains an event. Those domain proofs are still
recomputed; changing a placement cannot discharge a guard or a property write.

Independent verification reconstructs candidates and interval equations from
the current graph/plan, checks unique consumption and requires every selected
assignment to be consumed. General value-flow verification still establishes
availability, domains and original effect contracts. Failed or stale evidence
does not become permission to print source.

Normal snapshot construction runs independently of whether the function has a
phi. A subsequent logical-LHS ownership proof queries the original forest via
`BeforeLogicalFusion`; its exclusive predicate/keep-edge identity proof may
supersede the provisional snapshot. Extra observations retain the private slot.

## Source composition

The forest passes actual `UseId`s through scalar operands, calls, module stores
and native logical/conditional predicates. The exact selected use becomes an
assignment to the original private symbol; other uses remain reads. Operand
verification receives the same occurrence, checks the assignment's target and
original producer, and rejects a forged save or a changed private symbol.
For a native logical keep edge, the checked logical-left node supplies its
already evaluated result. It never executes the save again. Other selected
edges still use the ordinary private-slot proof; a producer being a snapshot
does not make every phi input a native logical-left occurrence.

A native member Reference evaluates its base once. Its suppressed VM receiver
is verified against the original property base occurrence, including a save
assignment at that occurrence. `VerifyTreeFlow` freshly verifies the evaluation
plan before operand checking. The operand checker therefore does not add a
same-block/cross-placement whitelist to the already proved order/availability.
The actual property result, consumer, base/receiver identity and storage
obligations remain binding; this does not license callee rereads or `.call`.

## Complexity and remaining scope

Candidate, interval and verifier passes are O(operations + uses + blocks), with
O(operations + blocks) bounded scratch. Existing budget admission accounts for
their arrays/work. No per-value graph traversal or selection-count limit is
introduced. Tests exercise 0/1/200/201/402 transparent copy dimensions, register
permutations and three explicit historical profiles.

Current event proofs remain local normal-flow proofs. A split between a producer
and its first use retains the original standalone snapshot; it is not a VM
restriction and does not reject that otherwise supported source. Phased
use-site assignments still need the common commit/throw/catch version model.
Literal initialization and protocol projections retain their own obligations.
Do not expand these boundaries with opcode/graph-shape exception lists.

The source oracle independently compares decoded generated functions with
handwritten JS for eager writes, typeof, computed reads, equality, one/repeated
call arguments and computed-write refusal cases. It observes Proxy/getter/key
conversion events, original receiver, shared-value identity, mutation and
injected throws. These are generated-source behavior checks on Node, not pinned
Ark VM execution or whole-record semantic/style acceptance.

## Selection route exits (Round 46)

See [selection-consumers.md](selection-consumers.md) for typed exit evidence,
materialization boundaries, independent route verification and the deferred
joint predicate/keep-edge projection. A materialized selected value remains one
stored result; later reads do not replay either arm or merge independent getters.
