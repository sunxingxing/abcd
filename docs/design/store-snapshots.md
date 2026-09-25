# Store Receiver Snapshots

## Design

Continue unified local-variable recovery with final-AST local copy propagation.
This is the standard basic-block copy-propagation and dead-write principle,
restricted to field receivers until broader emitted-read/liveness analysis is
available. It introduces no dependency, no callable-kind dispatch and no source
text matching. Ordinary methods and constructors use the same pass.

Original SSA remains essential to earlier semantic proofs but is not an exact
read/write model after result retargeting and expression ownership. Therefore
FoldStoreSnapshots inspects final AST reads and commits instead of declaring a
copy dead from its original SSA use count.

Maintain one pending plain register-to-register copy per basic-block traversal.
An audited SourceMemberStore may replace its copied receiver with the original
source register. The source must not have been overwritten since the copy.
Other reads of the copied register, unknown expressions, owner protocols or
source writes discard the pending candidate. A later stored assignment to the
copied register removes the old copy only after proving its RHS does not read
that old value. A block end alone is never proof of deadness. Replacements made
before a barrier remain valid, but the original copy is retained unless one of
the exit proofs below applies.

A return or throw can discard a pending copy when its emitted value does not
read the copied register. At a block exit, preserve the candidate through an
unconditional jump only; conditional predicates remain barriers. After local
rewriting, index each block's first actual operation once. A pending tail copy
is dead only if every direct normal successor starts by overwriting its target
without reading it, or exits without reading it. There is no alias propagation
into the successor, no multi-hop traversal and no resume-edge admission.
Empty blocks, unsupported first operations and owner prefixes give no proof.

First-operation indexes reference the current AST. If another deletion changes
that operation to a no-op, a later query conservatively fails; it never uses a
cached kill claim after the underlying assignment disappears. This is a bounded
first-access certificate, not complete CFG liveness recovery.

Calls, allocations and getters are not moved, duplicated or deleted. Private
registers are not lexical cells; property writes cannot mutate these slots.
Crossing an existing SourceOwnStore checks its receiver and value for reads
but never rewrites its private-object provenance or receiver. Catch regions,
phased SSA and branching argument regions are excluded. Staged-call and array
owner intervals are skipped in full. Super's certified argument interval is
skipped, not the whole function. Fused/constructor/literal/class owners and
nonempty previous-origin nodes remain barriers.

ReadsRegister returns a typed absent/present/unknown result. Its exhaustive
expression visitor inspects scalar operands, simple member receivers, string
comparisons and ordinary/member/constructor call inputs. Register ranges use
constant-time containment; fixed call arguments have the ISA's fixed bound.
Unreviewed protocols return unknown, never an assumed absent read.

## Cost And Provenance

For I final statements, B basic blocks and E normal edges, traversal is
O(I+B+E), with O(B) scratch storage for first-operation indexes and pending
tails. The common AST owner admits this vector against its remaining scratch
budget after retained allocations; allocation failure reports ResourceLimit.
The local scan itself keeps one candidate, which may miss unrelated
overlapping copies; this deliberately bounds the initial implementation and
does not discard semantics. Do not replace it with per-copy suffix scans.

Only receiver operands and a proved-dead copy's operation change. The store
keeps its PC; the erased copy remains a zero-width mapped statement. No source
span is recovered from generated text. Declaration cleanup remains the common
printer's emitted-slot census, not a register-name heuristic.

## Evidence And Limits

ABC-only pasteboard export removes five receiver-copy statements from its
PasteBoard constructor and emits direct this stores for pasteController,
pasteTextController, pastePixelMapController, editController and pathStack.
All six page outputs also recover receiver accesses in ordinary methods such
as setInitiallyProvidedValue. The direct-successor/exit extension additionally
removes 22 copies across those six ordinary methods: Home 4, PageA 1, PageB 1,
PasteBoard 10, ProgressBar 5 and ShareDitect 1. Home's captured receiver is
unchanged. Accumulator-to-field expression composition remains unfinished.

Production abc_structure and abc_semantic builds succeed. Export retains eight
incomplete sources, two refused records, nine metadata-only records and 195
mapped methods / 9,745 position entries. Read-only review found no concrete
defect. Tests, generated-JS execution, benchmarks and RSS tracking remain paused
at the user's request; output inspection is not an equivalence test.

Deferred regressions: source overwrites, target reads on RHS, multiple receiver
uses, throwing overwrites, successor reads, loops, own-store provenance, range
endpoints, every owner protocol, PC coordinates and 200/201 scaling. Broader
local live-range splitting, multi-hop dead-copy elimination and expression
composition remain separate unfinished milestones.
