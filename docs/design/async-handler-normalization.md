# Async Handler Normalization

## Preserved Exception Tables

`ProveAsyncCompletion` no longer requires the compiler completion catch to be
the only exception region. It still requires the exact ENTER/state-store and
RESOLVE/RETURN, catch-store/REJECT/RETURN protocol with private state ownership.
The completion region and its sole untyped handler must be last in encoded
order, cover precisely the expected body and rejection tail, and match the
decoded code end. This is an intentionally narrow compiler-protocol proof.

All earlier try regions and handlers must lie inside the completion region.
Their handler slices cannot reference the removed completion handler. They
remain in original file order, with unchanged handler types and slice indices;
sorting overlapping regions would change runtime dispatch precedence.

`async_handlers.cc` maps each retained range through the normalization's original
PC index. A boundary must name a retained instruction, a removed RESOLVE
immediately followed by its retained RETURN, or the removed final catch entry
which maps to the new code end. Other deleted boundaries are refused. The
existing await-region proof independently rejects try/handler boundaries and
incoming branches inside a collapsed resume protocol. No user catch is removed.

For I instructions, T try regions and H handlers, table validation is O(T+H),
and rebasing is O((T+H) log I), with O(T+H) retained output. The instruction/PC
index already belongs to normalization. Each vector capacity is admitted while
the await proof, instructions and provenance remain live; all callable owners
and the inspection survey subtract retained try/handler capacities too.

## Current Integration Boundary

Normalization produces bytecode with preserved user exception tables. The source
owner now accepts it only with the typed ownership and rejected-register proof
described below. Shared await lifting feeds phased exception SSA; ordinary
normal-flow lifting cannot replace that SSA or recover catches by printing text
around a body.

ProgressBar child 10333 provides the target evidence: its user catch starts at
PC352, while the compiler rejection tail starts at PC437; await PC320 rejects
through the former before the outer completion path. It also allocates a named
lexical environment at PC0 before ASYNCFUNCTIONENTER at PC19. The current
normalizer still refuses that independent non-MOV prologue. Removing this
restriction without accounting for environment ownership is not authorized by
this table-remapping change.

Both production CLIs compile and read-only review found no concrete defect.
ABC-only inspection still emits the existing onBackup 5437 and onRestore 5465
body artifacts; 10333 remains refused at normalization. No new complete record
or runtime equivalence is claimed. Tests, generated-JS execution and RSS remain
paused. Deferred checks include overlapping regions and priority, nested catches,
shared handler slices, early settlements, deleted await-interior boundaries,
branch bypasses, 200/201 scaling, and memory/output failures.

Separately, PageA callback 7061's PC53 consumes an ordinary call result whose
value domain is Unknown. Pending-exception `language_and_host_v1` admission is
not silently extended to normal returns. A separate user confirmation was
requested; any accepted normal-return premise must be typed and versioned,
without weakening explicit operand/return verification.

## Shared Await Lifting

Normal and exception CFG construction now use the same borrowed `SemanticLifter`
cursor. It produces `AwaitValue` with accumulator input/output, suspension and
user-code effects, and a possible exception, followed by an exact NEXT mode on
normal continuation. It never reads the discarded private state register in SSA.
Profiles, ordered/nonoverlapping await-mode certificates, raw operand shape,
state identity, skipped PCs and unconsumed certificates are checked. Without
certificates, raw await/resume instructions remain unsupported.

`BuildExceptionFlow` accepts the same optional typed certificates. Its Before
node can route to the existing catch gateway without visiting the result Commit.
The existing exception SSA and value solver thus see the normalized operation's
effects, not the VM opcode's register effects. Cursor work is O(I+A), storage is
O(1) excluding borrowed certificates, and per-instruction lifting remains charged
to the graph quota. There is no second owned semantic-instruction array.

This is deliberately not original-protocol equivalence. On rejected resumption,
the original VM sequence writes its result/mode temporaries before THROW. Native
await instead throws before the normalized result Commit. A user catch could
observe those different register snapshots even when its selected handler is
unchanged. The async source owner initially rejected rebased exception bodies
until that observation could be ruled out; the next section describes the new
proof. The table-remapping certificate alone does not establish equivalence.

Both production CLIs compile after this integration, and read-only review found
no concrete defect. ABC-only export to `build/pasteboard-shared-lift-01` still
publishes CommonConstants, Logger and Home as explicitly incomplete files under
their ABC-derived source paths. Seven application records remain refused; nine
records are metadata. No record-completion increase or execution proof is
claimed. Deferred validation additionally includes rejected-await catch reads of
result/mode registers, overwritten/dead temporaries, nested rejection routes,
certificate gaps/duplicates, unsupported profiles and graph-quota exhaustion.

## Rejected Registers and Source Ownership

`ProveAwaitCatchInputs` consumes the original decoded body and verified await
regions. The selected dispatch target at await and its rejected-value THROW
must agree. The final, independently proved compiler rejection handler cannot
observe result/mode temporaries; other catch entries require first-use proof.

For each queried register at a user catch entry, a straight-line prefix must
overwrite its incoming value before reading it, or exit the function without
reading it. Unknown instructions, branches and possible further user catch
transfers end that proof conservatively. A possibly throwing instruction's
normal write never kills the incoming value on its local exceptional edge.
Reads win over writes of the same instruction. Fixed and ranged call operands
are included, without expanding large argument windows into individual slots.

The implementation independently combines an offline reverse instruction sweep,
coordinate compression and a lazy range-assignment segment tree. This standard
indexing technique introduces no dependency, imported implementation or license.
Only the queried result/mode registers are indexed. All catch probes share one
sweep; there is no per-await handler scan or frame-sized reset. With I
instructions, A await regions and R dispatch spans, cost after dispatch
construction is O((I+A) log(A+1) + I log(R+1)); extra storage is O(A), plus the
retained dispatch index. Allocation capacities, sort/query work and tree visits
are admitted explicitly. Timing/scaling/RSS measurement remains deferred.

After this proof and existing normalization checks succeed, ordinary async,
async method and async arrow owners set `BodyLayout::kNormalizedAsync` on their
own MethodConstants. Child expansions reconstruct constants and do not inherit
that premise. The exception source owner requires matching monotone original-PC
provenance, lifts typed awaits into exception CFG/SSA, and sets IR origins before
AST printing. Original bodies with unexpected normalization facts still refuse.

Exception environment topology now permits suspension only for typed AwaitValue
with no environment-write effect. In the pinned v7 runtime,
`RuntimeStubs::SaveFrameToContext` (`stubs/runtime_stubs.cpp`, around 3914) saves
the current lexical environment and `EcmaInterpreter::GeneratorReEnter`
(`interpreter/interpreter-inl.cpp`, around 868) restores it. This preserves
environment identity across normal/rejected resumption, not captured cell
values. Other suspension remains unsupported.

Production CLI builds and read-only review completed. The review's dispatch
lookup complexity omission and missing suspension/environment gate were fixed.
ABC-only export `build/pasteboard-await-catch-01` still publishes the same three
explicitly incomplete records. ProgressBar 10333's named-environment prologue
is still refused, so its full normalized exception path is not yet exercised
by a published record. No runtime equivalence or record completion is claimed.
Tests, JS and RSS were not run. Additional deferred cases: read-before-write,
write-before-read, ranged operand reads, throwing normal writes, handler branches,
shared catch targets, unknown operations, environment identity versus cell
mutation, capture expansion isolation and low memory/work budgets.

## Final Settlement and Actual PasteBoard Path

The rejected-register proof now recognizes the exact final RESOLVE/RETURN pair
immediately before the independently proved completion handler. With no user
catch selected there, that pair exits; its typed state and value operands still
count as reads. Other coroutine instructions and settlements routed to a user
catch remain Unknown. This closes a false refusal where a catch never reads the
resume temporaries but exits through the compiler's normal settlement.

Actual owner inspection follows PasteBoard entry 8831, class constructor 9277,
child 9082's DEFINEFUNC at PC145, and async child 9165. The constructor already
produces a 193-span shared body. Child 9165 has a MOV-only prelude and four awaits;
its user handler at PC994 defines child 9221, then falls through PC999's undefined
load, RESOLVE at PC1000 and RETURN at PC1002. Compiler rejection starts at PC1003.
Its catch-register proof now succeeds (125 charged work units in this inspection),
and the actual owner reaches shared exception region construction after phased
SSA, environment and input validation. The next refusal is a backward jump at
original PC852, reported at its normalized block end PC766. Exception region
construction still supports forward edges only, although normal regions already
have a typed RegionLoop. No emitted record completion is claimed.

Both production CLIs build and read-only review found no concrete defect; no
tests, JS or RSS ran. Deferred cases include watched state/value operands in the
final settlement, user-catch dispatch on that settlement, nonadjacent/malformed
tails, shared handlers and loops around await.

ProgressBar 10333 remains independently refused at its environment-allocation
prologue. Pinned `Heap::AllocateYoungOrHugeObject` can call the OOM error path;
moving a pre-promise allocation into native async may change synchronous throw
to promise rejection on resource exhaustion. The user has been asked whether
VM-internal resource-exhaustion differences are outside source equivalence.
No such premise has been adopted and the decompiler's own 1G limit is unchanged.
