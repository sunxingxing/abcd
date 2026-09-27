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

### Current Function Identity

LDFUNCTION now lifts to FrameFunctionLoad and imports as ValueFrameFunction.
The pinned interpreter handler at interpreter-inl.cpp:3267 invokes GetFunction;
the implementation at :7982 reads InterpretedFrame::function. It neither reads
the optional ABI function register nor resolves a source name. The result is a
defined language value, writes only acc and has no ordinary JS abrupt effect.
Three-profile analysis tests cover absent ABI slots, overwritten ABI copies,
malformed operands and mutated IR correspondence.

Named ordinary functions now traverse the shared source pipeline. BindingPlan
allocates one immutable FunctionSelfBinding per owner with live frame-function
reads; the expression forest emits SymbolExpr references. The function-name
scope is outside parameter/body scopes and inside capture/module scopes.
Tree structure, flow, operands and ABI independently check its identity,
immutability and correspondence. ABI distinguishes frame reads from source
references and reports FrameIdentity rather than Unobserved. Family assembly
rechecks the creation-proven runtime name. The existing printer emits a named
function expression, not a body declaration or a lookup of the mutable name
property. CFG blocks, edge copies and lexical-cell allocation ownership do not
change. Added binding admission is O(I); existing name indexes remain bounded.

Twelve file-backed variants across three profiles cover direct/callback reads
with and without overwriting the ABI function copy. Generated JS checks self
return, constructor return, ignored callback results, thrown identity, reentry,
outer-variable rebinding and changing the function's name property. Four more
cases cover $arg0 (must not capture a synthesized parameter), __proto__,
MethodSelf1 and ClassValue. Each load PC maps to the exact self identifier span.
Two narrow/wide store families capture the parent's function identity into a
child arrow's cell, interleave activations and exercise key exceptions/setter
reentry. Incorrect explicit object parameters and call receivers cannot replace
the captured function. AST mutations test parameter substitution/shadowing,
missing/mutable self, wrong scope, illegal name and unsupported callable kinds.

The creation-owned extension below now also covers empty/illegal runtime names,
direct arrow reads and name conflicts. Sixty file-backed variants cross three
profiles, five names, ordinary/arrow callables and direct/callback bodies, all
with overwritten ABI copies. An empty runtime name comes from present mangled
metadata `#`, never absent metadata. Execution checks name descriptors, length,
constructibility, identity, exceptions and reentry. Twelve additional variants
preserve same-name global getters and their thrown values; two narrow/wide
families preserve descendant global reads matching the parent's runtime name.
Parameter-name conflicts and invalid form/evidence/contract mutations have
AST-level tests, not file-backed debug-parameter execution evidence.

This is not all LDFUNCTION or group coverage. Missing name metadata, Unicode
names outside the upstream name boundary, methods and async/derived owner forms
remain unsupported. Node tests are not Ark VM differential evidence. Requirements
and the invariants of the implemented steps are:

1. Retain the explicit self-binding scope outside the parameter scope and
   inside enclosing capture/module scopes. A self binding is initialized by
   function creation, is immutable, and is not a body let declaration. Preserve
   this distinction in TreeScopeCheck, TreeFlow and ABI census. Parameter and
   body shadowing must not silently change a frame-function reference.
2. Resolve each self reference against the actual FunctionOwnerId and creation
   occurrence, not a method name or offset alone. Verify the value-to-symbol
   correspondence independently; track frame reads separately from ABI copies.
   Captured self references use the existing family capture ownership checks.
3. Named-function expression syntax can provide an immutable binding only when
   its identifier is legal, preserves the proven runtime function name and
   does not capture a global lookup or get shadowed by parameters/locals.
   Never simply print the metadata name for LDFUNCTION. Invalid/empty names,
   shadowing and arrows use the verified creation-owned binding representation
   preserving callable kind, name and identity. Methods still require their own
   owner/home-object proof; this is not permission to narrow the overall ISA goal.
4. Extend the unified printer and provenance tables only from the verified AST.
   Do not use arguments.callee, mutable object-property lookup, a global name,
   source-text rewriting, or an extra ad hoc body writer as a substitute.
5. End-to-end acceptance: the same encoded function traverses import, SSA,
   evaluation, scoped AST, verification and printing; Node checks self-return,
   recursive/reentrant calls, normal and throwing callbacks, overwritten ABI
   slots, renamed/rebound outer variables, changed function.name, distinct
   creation activations, nested captures and PC-to-expression positions.
   Include shadowing and mismatched-owner mutation failures. Retain profile
   separation and distinguish Node source checks from Ark VM differential tests.

Scope/name indexes and owner lookup must remain bounded O(N log N) or better;
do not scan all symbols for every self read. Charge new scope, binding and
provenance storage to the existing method/family budgets.

#### Creation-Owned Binding Extension

Keep named-function self as the direct representation when its name is legal
and cannot shadow parameters, captures, modules or global references. Otherwise
use a typed creation-binding form, not a text rewrite: an unexposed arrow
creation envelope initializes one private const to the existing NamedEvaluation
function expression, then returns that exact function. The target body only
executes after const initialization. Literal computed naming cannot invoke user
code or expose the half-initialized function. The envelope inherits lexical
this/arguments/new.target and introduces no ordinary call receiver or arguments
binding. Target function kind, signature, name descriptors and constructibility
remain unchanged. This extends to ordinary functions and arrows, not methods
whose home-object or async/derived protocols still need their own proof.

The AST must distinguish named-expression and creation-binding forms, with
the existing immutable self scope representing the relevant name environment.
Only the unified printer may lower that verified form. Creation-owned bindings
use collision-checked synthetic names, never the runtime name as an identifier.
For owners with child function expressions use the private form so adding a
named-expression environment cannot capture descendant free names. Family
enclosure validation still checks actual descendant references. Admission uses
the existing resource-failure exclusion, not a new host assumption; mutating
the form, name, declaration, owner or scope must fail independent validation.
The implemented tests execute empty/illegal names and arrows across profiles, prove exact
self identity after ABI-copy overwrite, preserve name/length/prototype behavior,
and exercise parameter/global name collisions and nested captures.

### Realm Object Dependency

The pinned interpreter initializes `globalObj` from
`globalEnv->GetGlobalObject()` at interpreter-inl.cpp:1074 and LDGLOBAL assigns
that object to acc at :1750. Existing GlobalObjectLoad now imports as a distinct
ValueGlobalObject with no operands, a Defined result and no ordinary abrupt or
observable effect. It is not ValueGlobalRead and carries no source spelling.
Independent import correspondence rejects substituting frame-function identity.
Three-profile tests cover import, result domain, operand shape and malformed
encodings. Retention and evaluation ordering remain conservative. The shared
source route now lowers this value to RealmObjectExpr, not GlobalNameExpr;
independent correspondence rejects substituting a name read or ThisExpr.

The implemented publication path follows these steps:

1. Add a dedicated realm expression to the shared AST and independently match
   it to ValueGlobalObject. Do not represent it as GlobalNameExpr: global name
   lookup can invoke getters or throw, whereas the VM identity load does neither.
2. Resolve its generated binding at family publication. Under the existing V2
   contract, stable unshadowed globalThis is admissible; V1 requires an explicit
   HostRealmBinding. Record the chosen requirement in emission evidence rather
   than silently acquiring the trust boundary from an identifier spelling.
3. Verify local, parameter, capture, module, class and descendant enclosures
   using the existing indexed scope checks. A conflicting spelling must either
   get a proven collision-free binding or reject; never shadow user references
   or introduce a new implicit host premise. Preserve source origin intervals.
4. Execute three-profile direct/callback and nested-capture fixtures, checking
   exact realm identity, ignored callback results, reentry and thrown values.
   Add V1 missing-host, malformed binding, shadowing and AST-substitution
   negatives.

The file-backed fixture now executes 24 variants: three profiles, ordinary and
arrow callables, direct/callback bodies, and V2 stable-globalThis/explicit-host
bindings. It checks exact object identity, wrong call receivers, ignored callback
results, reentry, thrown identity, PC-to-identifier spans and parsed JSON binding
evidence. A changed destination and a realm draft without context both reject
before writing bytes. Two narrow/wide lexical-cell families capture LDGLOBAL
into child arrows, then exercise property-key exceptions and setter reentry.
Scope tests distinguish ancestor/TDZ bindings from sibling blocks, validate V1
host requirements and malformed names, and reject AST substitutions.

PrintedFunction owns the selected policy/name; the position artifact serializes
that requirement and source-contract version. Every publication rechecks the
actual destination using the existing sorted Euler-interval binding index;
there is no per-read ancestor scan and no CFG/SSA/cell ownership change. Name
selection and character comparison are charged to the existing work budget.
These are method/family source results, not whole-record or all-context coverage.
V1 explicit-host resolution has scope-level evidence, not an executable named
creation fixture: V1 separately refuses the naming wrapper's allocation premise.
Generator/async/derived owners still require their existing protocol proofs.

### Frame Receiver Dependency

LDTHIS lifts to FrameThisLoad with an accumulator definition and no
register read. The pinned interpreter handler calls GetThis, which returns
InterpretedFrame::thisObj (interpreter-inl.cpp, GetThis implementation), not the
optional writable ABI receiver slot. Its default value rule remains Unknown: an
uninitialized derived receiver cannot be promoted to a JS value just because
an ABI slot contains one. Tests retain that distinction for 12.0.2.0,
12.0.6.0 and 13.0.1.0 and reject exposing the unconstrained result.

Initialized ordinary callable owners now carry an explicit FrameReceiver
premise through SemanticProof and the value solver. Ownerless diagnostics stay
Unknown. ValueFrameThis has no ABI operand and lowers to owner-qualified
ThisExpr; operand verification requires a language result and the right owner.
FunctionAbi counts frame reads independently of ReceiverEntry, including when
no ABI this slot exists. The immutable family owner is rechecked at publication.
Methods containing SuperRange do not receive a whole-body initialized premise.
Ordinary/base initialized receivers, derived
pre-/post-super states and arrow lexical receivers must remain distinct.
Do not infer this evidence from frame slot existence or method-name spelling.
Twelve file-backed variants across three profiles now emit and execute this:
direct/callback forms, with and without overwriting the ABI receiver copy.
Node tests cover call receivers (including primitives, undefined, null and
Proxy identity), constructor invocation, callback arguments and thrown identity.
Three raw arrow-LDTHIS fixtures still reject; owner/domain AST mutations and
arrow/derived ABI misuse also reject. Separate narrow/wide computed-store
fixtures capture a parent's LDTHIS into an arrow's lexical cell, interleave
factory activations and exercise throwing keys and reentrant setters. They
pass the wrong explicit object parameter and a different child-call receiver
to distinguish the identities. This does not implement raw arrow LDTHIS,
derived pre-/post-super phases, or async receiver ownership.

Concrete integration order:

Steps 1-4 are now wired for initialized ordinary owners. FunctionInput stores an immutable creation stamp
(creator, PC, optional class property, callable, strictness and complete frame)
only from an already resolved FunctionContext. It rejects mismatched file
identity, target and frame before analysis. Family tree construction checks
the stamp against its actual owner again. The ownerless diagnostic path stays
available and does not acquire an owner after verification. Only supported
initialized callable kinds grant the explicit premise; step 5's derived and
arrow phase/capture extensions remain unfinished.

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
