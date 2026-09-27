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
Source rules remain version 42 because no source publication is enabled.
Existing legacy normalized async paths are not replaced or broadened.

Historical three-register settlement and raw suspend/resume remain outside
this slice. The extra deprecated operands must not be silently dropped using
modern encoding assumptions. The typed decoder is not a protocol owner proof.

## Remaining Integration

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

1. Follow T09 with a phase-indexed value import: preserve Before, Commit, Catch,
   exceptional edges, original instruction identities and handler priority.
   Do not index catch definitions as normal instruction results or reuse normal
   environment coordinates for phased capture/module proofs.
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
