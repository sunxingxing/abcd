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
before a barrier remain valid, but the original copy is retained.

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

For I final statements and B basic blocks the traversal is O(I+B), with O(1)
additional storage and no allocation. Keeping one candidate may miss unrelated
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
as setInitiallyProvidedValue. Copies at branch ends remain because this pass
does not yet prove successor liveness. Home's captured receiver is unchanged.

Production abc_structure and abc_semantic builds succeed. Export retains eight
incomplete sources, two refused records, nine metadata-only records and 195
mapped methods / 9,745 position entries. Read-only review found no concrete
defect. Tests, generated-JS execution, benchmarks and RSS tracking remain paused
at the user's request; output inspection is not an equivalence test.

Deferred regressions: source overwrites, target reads on RHS, multiple receiver
uses, throwing overwrites, successor reads, loops, own-store provenance, range
endpoints, every owner protocol, PC coordinates and 200/201 scaling. Broader
local live-range splitting, branch-aware dead-copy elimination and expression
composition remain separate unfinished milestones.
