# Capture Binding

T07 now shares a borrowed `ScopeFacts` view between the established semantic
capture resolver and the new creation-specific `CapturePlan`. This is binding
infrastructure, not permission to emit a child FunctionTree or scalarize cells.
Source rules remain 18; no closure source domain is newly admitted.

## Inputs and Ownership

The caller must supply one unchanged, freshly verified SemanticMethod,
EnvironmentFlow, LocalAccessPlan, DefinedCaptures and ClosureWrites premise.
ScopeFacts borrows them; it must not outlive them or mix methods. Local cell,
store, barrier and guard indexes are the semantic verifier's sorted indexes,
not names, debug scopes or guessed declaration sites. No per-query graph scan
or duplicate environment index is introduced.

CapturePlan also requires a successful BindCreation result in that same file,
profile and owner, and the child's proved capture-use obligations. It checks
the instruction PC, typed creation metadata, ordinary capture environment and
non-sendable target. It retains instruction, PC and target MethodPosition. Those
IDs are owner-relative, not a persistent cache key or independent file identity.
The immutable function publication owner must retain their identity envelope.

## Binding and Obligations

Each capture occurrence is rebased through the actual creation environment;
occurrences are neither deduplicated by target method nor merged by slot name.
Local bindings require an initialization certificate and the verifier's filtered
closure-write barriers. Defined demands retain the stricter guard intervals.
Unknown earlier creation writes cannot be ignored just because a store exists.
Outer captures remain typed CapturedSlot obligations with their original domain.
Returning one is not proof that the outer value is initialized or defined.

ClosureWrites::Language is conditional on complete child validation, including
transitive writes. It is not constness or a snapshot proof. Local EnvironmentId
identifies an allocation site; loop iterations and repeated parent activations
still create different dynamic cells. This component performs no cell-to-value
rewrite and does not change CFG, SSA, block or source-scope ownership.

VerifyCapturePlan rechecks the supplied occurrence count, creation context,
actual rebased cell and domain against ScopeFacts. It does not call the planner
or accept an emitter-provided name as evidence. ScopeFacts itself consumes the
semantic proof's preconditions; it is not a replacement independent verifier
of arbitrary caller-fabricated SemanticProof structs.

## Cost and Remaining Integration

For K capture occurrences, C indexed cells, W stores, B blocks, A barriers and
G defined guards, binding/checking costs
O(K (log(C+1)+log(W+1)+log(B+1)+log(A+1)+log(G+1)+32)). The final term is the
uint32 environment-depth domain's fixed ancestry bound. Retained storage is
O(K); query scratch is constant. Planning and its check share one work allowance,
and output capacity is admitted before allocation. These are invocation bounds,
not evidence of whole-process RSS compliance.

Regression sources cover 0/1/200/201/4096 captures, different allocation contexts
for the same target, outer Defined obligations, changed domain/cell/site,
sendable rejection, uninitialized locals, unknown write barriers and exact work
limits. They remain uncompiled/unrun during the user's testing pause.

Required next integration remains: typed environment/cell/creation operations in
Value IR with input/effect correspondence; capture-aware symbol binding and AST;
complete child trees validated before parent publication; dependency/SCC handling
without assuming success for cycles. P3 method 7366, independent-app examples,
per-iteration cells and child mutations still require end-to-end source evidence.
Neither this component nor its production build closes the T07 gate.
