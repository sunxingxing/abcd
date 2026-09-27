# Async Protocol and Exception Phases

This is the first prerequisite slice of T09/T10, brought forward for ISA group
1. It does not certify async source recovery or complete that group. Reference:
the revisions pinned in `docs/reference/openharmony-v7.0-release.lock.json`.

## Runtime Facts

- `interpreter-inl.cpp`, ASYNCFUNCTIONENTER: only successful runtime completion
  writes acc. `RuntimeAsyncFunctionEnter` in `runtime_stubs-inl.h` allocates a
  promise through the retained intrinsic, a JSAsyncFuncObject and a generator
  context. The returned value is the state object, not its retained promise.
- ASYNCFUNCTIONRESOLVE_V8 / ASYNCFUNCTIONREJECT_V8 read the state register and
  acc value. Only successful completion replaces acc with the retained promise.
  `RuntimeAsyncFunctionResolveOrReject` creates fresh resolving functions on
  every invocation. Repeated settlement is not a removable duplicate operation.
- Resolution can synchronously access a thenable's `then` property; rejection
  can notify the host's rejection tracker. Settlement is ordered, allocating,
  heap-reading/writing and potentially executing user/host code. Its effects
  before a throw must not disappear.
- LDNEWTARGET in the pinned interpreter remains a fatal unimplemented handler.
  It is not evidence for emitting native `new.target`.

## Shared Representation

`AsyncEnter` and `AsyncSettle{kind,state,value}` are SemanticOperation variants.
Settlement kind is an enum; operands retain their typed scalar slots. Modern
settlement decoding reuses LiftCoroutineInstruction, including v8 width and
frame bounds. Each instruction retains its original PC without bytecode removal
or branch rebasing. The admitted semantic profiles remain 12.0.2, 12.0.6 and
13.0.1; this does not create a new historical-profile permission.

BuildExceptionFlow and ExceptionSsa already provide the required topology:

```text
Before(state/value reads, observable effects)
  normal -> Commit(acc result) -> next Before
  throw  -> Catch(acc exception) -> handler Before

normal jump to handler --------------------> handler Before
```

Before never defines the normal result. A handler keeps pre-throw register
values; only the synthetic Catch defines the pending exception accumulator.
Normal entry to handler code bypasses Catch and may require an accumulator phi.
Enter failure outside the protected body escapes; it cannot reject a promise
through a state object whose creation did not complete.

The new lifting is O(1) work/storage per instruction. Exception graph/SSA work
and storage reuse their existing bounded algorithms; no per-state graph walk,
new normalization writer or speculative source expression is introduced.

## Admission Boundary

Typed register-value rules explicitly refuse these raw protocol operations.
The normal value importer likewise has no async operand/operation lowering.
Thus successful semantic lifting/phase SSA is not Language-value permission,
an executable receipt or permission to output `Promise.resolve`/native async.
Raw async lifting does not itself enable source publication. Current source
rules are 44 after the separately integrated explicit-throw source path;
the V3 source-text contract is unchanged.
Existing legacy normalized async paths are not replaced or broadened.

Historical three-register settlement and raw suspend/resume remain outside
this slice. The extra deprecated operands must not be silently dropped using
modern encoding assumptions. The typed decoder is not a protocol owner proof.

## Remaining Integration

The common ValueProgram now retains one ValuePhaseSite per phased block. Its
input ExceptionNodeId is separate from the original InputSiteId and PC. Before
owns the original operation/effects, OpResult names its Commit availability
block, and CatchDefinition names an independent exception value. Exceptional
edges retain a typed label. VerifyPhasedValueFlow reuses the normal verifier's
dominator and sparse phi-input checks, but returns a distinct proof. Its private
normal-flow adapter is topology-only, not a source lowering or effect rewrite.

This proves value availability, not bytecode correspondence or handler priority.
ImportPhasedValues now maps unchanged ExceptionFlow/ExceptionSsa into this
model. VerifyImportedPhases independently compares phase identities, original
PCs, operations, effects, results, every SSA read occurrence, phi inputs, entry
ABI and value categories without rebuilding a second graph. Entry/literal
matching and operation/operand mappings are shared with normal import.
ExceptionSsa exposes a sequential phi-input span so neither import nor checking
performs one graph scan or binary lookup per phi. InputRead remains an original
instruction coordinate; ValuePhaseSite retains the separate phase coordinate.

ValueExceptionalExit retains an optional ValueHandler containing the selected
TryId, HandlerId, handler PC and Catch block, including the distinction from
propagation to the caller. Normal input correspondence refuses this handler
metadata and rewrite checking cannot change it. The low-level phase input
bundle borrows the same immutable flow/SSA/environment/value analyses, validated
input proof and frame. Import rechecks the input proof under its shared budget;
this does not discharge its host/module/capture premises or turn an environment
coordinate into a lexical binding/source-scope proof.

Explicit ThrowOperation now imports as ValueThrow with a kThrownValue operand.
Its Before has only an exceptional successor (or propagation to caller), never
a normal result. The provenance-only, unreachable Commit remains in the graph
with no operations or edges. Availability permits precisely this phase when
its throw producer is reachable; it does not admit arbitrary unreachable
instructions, definitions or handlers. Input correspondence checks ThrownValue
against the original operand rather than treating it as an unknown pending
runtime exception. Internal VM values still fail the independent input proof.

Other unreachable phases still reject before source publication. Normalized resume origins
also reject until an owner-certified lowering is integrated. Ordinary import
correspondence, simplification, rewrite, evaluation and control planning reject
phased graphs until their exception-aware integration exists. Removing the
phase table cannot turn Commit results or Catch definitions into normal values.

Verification retains the shared dominator bounds. Added topology checking is
O(B+E+O), availability checks are constant time per occurrence, and sparse phi
keys are sorted in O(U log U); storage is O(B+V+U) plus the existing dominator
scratch. Work and byte budgets are shared with that builder, not reset per
phase. `phased_value_flow_test` covers normal/caught value separation, handler
phis, normal handler entry bypassing Catch, wrong Commit/edge/handler metadata,
normal-only refusal, exact work and zero-memory limits, and 200/201/402 phase
pairs both straight-line and cyclic. These are synthetic common-IR tests,
not new executable exception or async source coverage.

`phased_value_import_test` adds decoded ABC -> exception graph -> SSA -> input
proof -> common value IR -> independent correspondence coverage. It includes
parameter snapshots with 0/1/200/201/402 formals under all three admitted
profiles, normal handler entry phis, environment allocation/pop surrounding a
snapshot, a throwing property read whose catch sees the old local, and forged
PC/phase/effect/handler/domain/read/frame inputs. The checker has exact work
boundary tests; the importer refuses zero work/memory. Import construction is
O(N+U+V+E+S log V), plus the shared availability verifier's dominator/phi bounds;
storage is O(N+U+V+E) plus that verifier's scratch. Inputs remain caller-admitted.
These synthetic inputs do not prove full source or corpus completion.

## Explicit Throw Source Slice

As a T09/group-1 dependency (not completion of group 8), THROW_PREF_NONE now
also traverses the normal no-handler pipeline: decode, typed semantics/SSA,
value import, simplification, control/evaluation, ThrowStmt, independent AST
checks and the shared printer. ThrowStmt owns a required expression; it is not
ReturnStmt or an expression statement. No coercion, synthetic Error allocation,
normal result or fallthrough is introduced. Throw remains ordered and cannot
be erased, even if its effect flags are malformed. Rule 44 invalidates earlier
source evidence while preserving the existing V3 premises.

The file-backed constant-group fixture adds 39 functions across the three
profiles: six constants with/without a preceding callback and strict receiver
throws. Generated JS checks undefined/null/NaN/infinity/boolean payloads,
receiver identity including -0/bigint/symbol/Proxy, absence of coercion, callback
effects and earlier exceptions taking precedence. Original throw PC maps to
the generated statement span through the AST, never text reconstruction.
Tree tests reject throw-to-return/expression mutations and invalid operands.
Phase tests cover propagation, catch entry, invalid completion/payload, internal
Hole rejection, 200/201/402 prelude instructions and refusal of ordinary dead
code after throw. Both import and file-backed source tests failed before this
implementation. This does not yet emit try/catch/finally or native async.

Argument snapshots now compose with this phase graph: BuildExceptionFlow takes
the same optional FrameLayout as normal lifting and retains a value copy.
VerifyExceptionInputs requires an explicit ArgumentFrameFact matching it, even
for zero formals, and retains the accepted fact in its result. It checks the
same typed snapshot effects/window as the normal path, the Before SSA read
ordinals/slots, the matching normal Commit definition and Language categories.
The lower-level fact is a caller-certified premise, not permission inferred
from layout equality. A future file/source owner must supply and recheck the
actual creation identity before publication. Existing source callers do not
invent that premise; they continue to refuse snapshots in exception bodies.

This adds O(N+U+S log V) checked work and O(1) extra verification storage (phase
nodes, reads, snapshots, SSA values). Tests include 0/1/200/201/402 formals,
normal snapshot versus Catch accumulator definitions, phase-coordinate facade
behavior, missing/wrong frame owners, internal slot values and exact work
boundaries. A lexical-prelude fixture checks that successful pop restores the
outer environment while snapshot failure reaches Catch in the inner one.
No exception AST or executable async receipt follows from these input proofs.

1. Complete T09 beyond the initial phase importer: general unreachable
   phases, completion, exception-aware region AST and environment/capture/module
   ownership. Do not index catch definitions as normal instruction results or
   reuse normal environment coordinates for phased capture/module proofs.
2. Prove callable creation identity and the state/promise relationship through
   SSA copies and joins. Reject escaping state, unrelated or merged owners,
   repeated settlement, bypassed settlement and mismatched returned promises
   unless a separate semantic proof handles those cases.
3. Prove the protected body and reject handler jointly. Native async conversion
   cannot absorb an original synchronous exception outside that protection.
   The existing V2 resource exclusion is not an exclusion of language/host
   exceptions and cannot serve as an owner proof.
4. Lower the certified completion into the shared source AST/printer, carrying
   original PC origins. Then extend the same model to suspension, resume modes,
   rejected awaits, finally and captured state across suspension.
5. Require file-backed callable fixtures and generated-JS execution before
   source completion, then paired and unpaired application samples. No sidecar
   source/map input, no dropped child functions, no pasteboard-only rules.

## Focused Evidence

`async_semantics_test` covers modern decode/effects and normal semantic SSA,
original PC retention, exceptional state and normal-handler entry phis,
malformed operand counts/kinds/widths/frame bounds, unsupported profiles and
deprecated settlement, source/value admission refusal, exact work boundaries,
zero memory allowance and 200/201/402 separate allocations. These are synthetic
analysis tests, not VM differential execution or end-to-end async source tests.
