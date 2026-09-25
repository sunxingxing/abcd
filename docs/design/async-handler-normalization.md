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

This change produces normalized bytecode with preserved user exception tables.
It does not yet grant the exception-source owner permission to accept rebased
async bodies. That owner still requires a normalization certificate and shared
await lifting into phased exception SSA. Ordinary normal-flow lifting cannot
replace exception SSA or recover these catches by printing text around a body.

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
unchanged. The async source owner still rejects rebased exception bodies until
that observation is ruled out or represented explicitly. The table-remapping
certificate alone does not establish register-snapshot equivalence.

Both production CLIs compile after this integration, and read-only review found
no concrete defect. ABC-only export to `build/pasteboard-shared-lift-01` still
publishes CommonConstants, Logger and Home as explicitly incomplete files under
their ABC-derived source paths. Seven application records remain refused; nine
records are metadata. No record-completion increase or execution proof is
claimed. Deferred validation additionally includes rejected-await catch reads of
result/mode registers, overwritten/dead temporaries, nested rejection routes,
certificate gaps/duplicates, unsupported profiles and graph-quota exhaustion.
