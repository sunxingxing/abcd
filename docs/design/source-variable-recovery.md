# Unified Source Variable Recovery

## Priority And Boundaries

Recover source bindings on the common function pipeline before expanding ISA
or corpus coverage. Constructors are callable kinds with extra receiver and
return constraints, not separate decompilation paths. Priorities are:

1. Parameter aliases, default-initialized parameters, local live ranges and
   source binding names; discard transport-only assignments/declarations.
2. Receiver identity and lexical captures, including dynamic function this,
   arrow lexical this, and derived-constructor initialization at super.
3. Expression composition and final dead-write cleanup without changing
   evaluation order, property effects, throws or observable captures.

The current implementation covers immutable parameter copies and a stable
super-result receiver binding (see receiver-binding.md). It does not recover
arbitrary local variables by renaming registers.
Register reuse needs value/liveness-based binding recovery; receiver captures
need explicit identity and scope evidence before substituting native this.
Do not replace all lexical cells with this or infer a local from its slot.

## Immutable Parameter Copies

FindEntryBindings scans the whole entry block for pure copies of caller-proved
immutable formals/ordinary receivers. Environment setup or other instructions
no longer stop candidate discovery. Every candidate is checked against all
original SSA definitions and reads: a read before its defining copy or any
other write rejects the candidate. Pure copy propagation is the standard
algorithm; there is no new dependency or source-text rewrite.

A ParameterDefault certificate, produced by the common IR/AST proof and
consumed by the owning native signature, permits the original copy and
removed default initializer to denote the same formal. Same-slot phis may
merge these roots. All other instruction definitions, MethodEntry reads and
MethodEntry phi seeds reject the alias. The proof audits all phi inputs in
one traversal rather than a separate graph walk for each parameter.

The final AST is audited again. Remaining stored assignments must still copy
the immutable formal verbatim; staged-call result targets and array targets
invalidate candidates. The write visitor is exhaustive. Entry-copy elision
requires a stored, non-field assignment; expression-only arguments must keep
their emitted value. The existing bound-read printer and slot census then
remove only declarations with no emitted use. Original PC anchors remain.

Default recognition now runs for every neutral-default callable kind, subject
to the same proof. Rest, catch and normalized-async boundaries remain guarded.
This changes no super/return/async contract and no superclass premise.

With I instructions, R registers, U reads, V SSA values, F phi inputs and P
recovered defaults, storage is O(R), admitted through sizeof(EntryBinding).
Traversal is O(I+R+U+V+F), plus existing indexed queries O(I log(U+1) +
P log(I+U+1) + V log(F+1)); no parameter-by-instruction scans or pairwise
alias comparisons are introduced. Performance measurements remain paused.

## Evidence And Remaining Work

ABC-only pasteboard export removes six parameter copies and six declarations
from each page constructor. Home now calls
`super(parent, __localStorage, elmtId, extraInfo)` and reads `paramsLambda`
and `params` directly. PasteBoard's ordinary arrow callback also replaces
its copied `$r3` with `$p0` in the buffer access and drops the declaration.
This is evidence of the common path, not just constructor-specific output.

Both production CLI targets built. Publication remains eight incomplete
sources, two refused records, nine metadata-only records and 195 methods /
9,745 position entries. Read-only review found no concrete defect. Tests,
generated-JS execution and RSS tracking remain paused; 1G compliance and full
application equivalence are not established by these exports.

Deferred regressions: overwritten formals, reads before copies, loop/phi entry
seeds, other local writes after defaults, argument-only owners, retargeted
results, captured variables, rest/catch/async boundaries, all callable kinds,
PC coordinates, admission limits and 200/201 scaling. Broader receiver/capture
recovery, general local live-range splitting and expression composition are
the next milestones, not completed work.
