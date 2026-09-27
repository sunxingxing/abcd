# Constant Object Loaders: Generic-Path Evidence

This is group 1 of the pinned v7 ISA plan, not a group-completion certificate.
The dedicated lifter accounts for all 18 encodings; that alone does not prove
that their effects survive the common source pipeline.

## Verified Slice

`tests/integration/constant_group_fixture.cc` covers LDNAN, LDINFINITY,
LDUNDEFINED, LDNULL, LDTRUE and LDFALSE with two bodies each:

1. Load and return the constant.
2. Save the constant, pass it to a callback, discard the call result and return
   the original constant. A throwing callback must prevent normal return.

The same encoded bodies run through profiles 12.0.2.0, 12.0.6.0 and 13.0.1.0.
All 36 positive cases pass the file-backed PreparedFamily/FamilyEmission route,
including shared semantic/SSA/value preparation, AST verification and printer.
Each requires a Printed/Complete method receipt and a nonempty generated
expression interval derived from the original constant instruction at PC 0.
The enclosing test module supplies the strict destination required by the API.
This is not a complete-record receipt.

The independent Node assertions check exact primitive results with Object.is,
function arity, lexical shadowing of NaN/Infinity/undefined, callback argument,
receiver, invocation count, ignored result, thrown-object identity and failure
for a non-callable input. Six additional LDHOLE cases require preparation to
reject direct return and host-call exposure of an internal marker. These are
negative domain tests, not positive LDHOLE decompilation coverage.

The fixture provides encoded method bodies and creation sites with explicit
metadata; it is not a whole-container parser test or a real-application sample.
It does not execute the original methods in an Ark VM, prove all historical
runtime behavior, or compare engine-specific error messages. Evaluation source
and assertions remain outside production inputs. No RSS or benchmark is run.

## Debugger Slice

DEBUGGER now enters the same pipeline as a typed DebuggerOperation,
ValueDebugger and DebuggerStmt, printed as `debugger;`. The pinned
`ets_runtime/ecmascript/interpreter/interpreter-inl.cpp` DEBUGGER handler calls
NotifyDebuggerStmt and dispatches without replacing the accumulator. Its IR
therefore has no register inputs/result, a debugger observation effect and no
ordinary JS exceptional completion. DCE cannot erase it, even if an effects
field is missing; evaluation planning keeps observable reads ordered across it.

The fixture adds two debugger instructions to each of the 36 positive cases.
Independent Node inspector events verify both pauses, the call between them,
and skipping the second pause when the callback throws. Both original debugger
PCs map to their exact generated statement spans. Mutation tests reject a
different AST statement in place of the debugger; an evaluation test keeps a
getter snapshot before the observation point. Malformed operand counts reject.

This preserves debugger statement occurrences and language execution order;
it does not promise identical VM register views, inspector protocols, stack
presentation or interactive debugger edits across Ark and Node. No debugger
inspection UI or bytecode-register reconstruction has been implemented.

## Remaining Group Work

### Frame Receiver Dependency

LDTHIS now lifts to FrameThisLoad with an accumulator definition and no
register read. The pinned interpreter handler calls GetThis, which returns
InterpretedFrame::thisObj (interpreter-inl.cpp, GetThis implementation), not the
optional writable ABI receiver slot. Its value rule remains Unknown: an
uninitialized derived receiver cannot be promoted to a JS value just because
an ABI slot contains one. Tests retain that distinction for 12.0.2.0,
12.0.6.0 and 13.0.1.0 and reject exposing the unconstrained result.

This is a semantic admission prerequisite, NOT LDTHIS source completion.
FunctionInput::Build currently verifies domains before the actual callable
context is bound. The next implementation must carry occurrence-bound receiver
evidence into that verification, retain it in the input owner, and revalidate
it at family publication. Ordinary/base initialized receivers, derived
pre-/post-super states and arrow lexical receivers must remain distinct.
Do not infer this evidence from frame slot existence or method-name spelling.
After that, a typed value read can map to an owner-qualified ThisExpr, with
independent ABI checks and tests for changed ABI copies, call/apply receiver
identity, capture ownership and derived initialization failures. The current
proof rejection is not counted as supported decompilation.

Concrete integration order:

Steps 1-2 are now wired. FunctionInput stores an immutable creation stamp
(creator, PC, optional class property, callable, strictness and complete frame)
only from an already resolved FunctionContext. It rejects mismatched file
identity, target and frame before analysis. Family tree construction checks
the stamp against its actual owner again. The ownerless diagnostic path stays
available and does not acquire an owner after verification. No receiver-domain
premise is granted by this wiring alone; steps 3-5 remain unfinished.

1. PreparedFamily::Prepare already owns a validated FunctionContext before
   calling ReadFunctionFamily. Pass that owner into root input construction;
   retain the current ownerless diagnostic overload as conservative.
2. FamilyReader currently calls ReadMethod(target) before resolving the child's
   CreationTreeContext. Resolve the actual occurrence first, then build the
   child input with that context. Preserve all capture rebasing and immutable
   file identity checks, and charge the context within the same budget.
3. Carry a typed receiver premise through FunctionInput, SemanticProof and
   register-value analysis. A default/absent premise remains Unknown. Reject
   mismatched identity, target, occurrence or receiver phase at publication.
4. Add the value-IR receiver read and owner-qualified ThisExpr correspondence.
   Extend FunctionAbi's receiver census to count these reads separately from
   ReceiverEntry operands; absence of an ABI copy must not forbid LDTHIS.
5. Prove initialized ordinary/base owners first without inventing a derived
   initialization or arrow capture premise. Then implement those owner phases
   using the same pipeline; keep them explicit unfinished group obligations.

- LDGLOBAL and LDSYMBOL: bind realm identity without replacing intrinsic
  identity by a mutable or shadowed global identifier.
- LDTHIS and LDFUNCTION: preserve callable ownership, constructor initialization
  and captured receiver/function identity in the shared callable pipeline.
- LDNEWTARGET: retain the pinned handler's unimplemented classification until
  profile-specific runtime evidence justifies a source representation.
- LDHOLE: verify supported internal consumption without exposing VM markers.
- Deprecated LDLEXENV and both POPLEXENV encodings: track environment ownership
  and actual runtime accumulator behavior, not merely YAML accumulator flags.
- GETUNMAPPEDARGS: preserve arguments identity, descriptors and frame ownership;
  do not substitute an ordinary rest array.
- ASYNCFUNCTIONENTER: integrate the complete async owner and exception phases,
  not a standalone Promise expression.

These are obligations, not assertions that no supporting implementation exists.
Each needs common-path positive/negative evidence and context validation before
the group can be marked complete.
