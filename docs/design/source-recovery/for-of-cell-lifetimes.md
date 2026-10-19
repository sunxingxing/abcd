# Cell lifetimes over a verified for-of projection

The original m28799 (o5/28303) has a verified ProtocolOwned control projection
and ForOfEmissionView. FunctionTree built that view before its lifetime check,
but CellLifetime rebuilt exception control without the ownership seal. It
therefore rejected the same control as kControl. Merely passing the seal would
also leave ProtocolCloseUnit interpreted as a PhaseControlUnit, and the done
branch would introduce a lexical arm scope absent from native for-of.

The lifetime checker now borrows the existing sealed ForOfEvaluationInput,
ForOfEvaluationPlan and final emission view. VerifyExceptionControl rechecks
original dispatch, close completion/ownership and original port correspondence;
VerifyForOfEmissionView independently reconstructs identity, iteration events
and projection membership. Neither builder flags nor a close-unit tag suffice.
Any missing or changed seal/plan/view/port keeps the control refusal.

The original loop discovery and LoopIds remain authoritative. Only a verified
native done decision's two edges share the existing loop scope: the success
binding/body is not a synthetic lexical if arm, and exhaustion creates no
binding or lexical scope. Ordinary decisions, nested loops and source body ops
keep their control identity. Close units retain their completion-keyed outcome
ports and ordinary continuation. Exception-only close entries have no source
lexical container, and their original phases remain unmapped; they are not
converted to transparent rethrows.

Block-to-source-scope entries are optional. Inactive, forwarded-handler and
implicit-close phases never acquire function scope by default. Every real cell
allocation and every access must have an explicitly visited scope. Existing
Euler enclosure checks still reject a use outside its allocation scope; original
CellPlan/environment validation preserves storage and activation identity.
Initialization/TDZ, capture, actual declaration placement and original/recovery
correspondence remain the existing independent tree obligations.

The new decision index, work stacks and scope arenas are prepaid. Additional
traversal is linear in control nodes, cell accesses and certified ports, with
one shared index rather than per-cell CFG rescans. Existing protocol verification
retains its own admitted-work/resource refusals and original inputs. Borrowed
plans/views remain alive and charged by FunctionTree; temporary verification
storage is released before the scope arenas are allocated.

## Evidence boundary

The real `for_of_cells` fixture's callback form publishes and passes 34 Node
engine/original cases, helper/accumulator isolation and a changed-accumulation
mutation. It covers scalar keys, captured function-local state, normal
exhaustion/continue and throw-driven close failures, iterator getter/call
failures, receiver identity and exception constructors. The unit test also
checks a stronger direct-cell-in-body graph's lifetime proof, absent close-block
scopes, source LoopId ancestry, missing seal, wrong loop/port and declaration
mutations, exact 200/201/400/800 graph budgets and copy/register/block variants.

Direct body-cell updates still stop at the existing recovery ownership gate;
explicit break/return shapes retain their unsealed close-ownership gate, and an
explicit throw shape retains its evaluation ownership gate. They are asserted
negative fixtures, not counted as engine-output differentials. This slice does
not broaden any of those independent proofs. Existing scope-escape and
catch-state tests remain enabled.

m28799 now reaches PC620 (op231), a member-call-reference refusal:
FunctionTreeError::kExpression > ExpressionError::kUnsupportedOperation >
CallPlanError::kReference > CallReferenceError::kNotInline >
EvaluationKind::kRoot > TemporaryReason::kCrossRegionValue >
GuardedCallError::kUse. The guarded source read is op20 PC56, guard op21 PC58,
and use257. This downstream refusal is parked. Under previous flags the original
kMultipleExits remains at m28799 PC1146, TryId0, handler PC1171.
