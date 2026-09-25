# Slot-Free Function Bodies

The subsequent `used-vm-slots.md` extension adds precise per-register facts for
audited mixed bodies; the original Unknown/None/Required contract below remains.

PrintedSource and PrintedTemplate carry a typed VmSlotUse result: Unknown,
None or Required. Unknown is the default for legacy/unproven output. The common
printer can publish an exact result only after the exhaustive bound-read audit
used by entry-copy elision. No generated JS text is scanned for register names.

Scalar emission first applies proved entry bindings, then observes actual
accumulator/register operands, including targets. A native-this binding and an
owner-proved immutable formal require no VM declaration; any other register or
accumulator does. Elided copies emit neither a read nor a write. Opaque class,
capture, buffer, global-name and other excluded paths keep Unknown. Observation
has constant scratch and bounded work per scalar expression. The enum adds no
dynamic storage, and template transport preserves it.

FunctionWriter prepares the generic body before deciding its VM prologue.
Only None omits the whole accumulator/register declaration group. Unknown and
Required keep the previous declarations and initializers; per-register pruning
in mixed bodies is not yet implemented. Body-local lexical declarations remain
owned by the shared printer. Positions and capture origins are forwarded only
after the actual declaration decision, so body coordinates use the correct
enclosing cursor. Legacy fallback remains conservative.

Preparing before declarations gives the body the current remaining output
allowance. After declarations, the remaining allowance is recalculated and the
owning FunctionWriter still enforces its total output limit. Oversized output
fails rather than silently dropping declarations or changing semantics.

Both production targets built successfully. ABC-only export
`build/pasteboard-slot-free-01` removes 102 declaration lines relative to
`build/pasteboard-entry-elision-01`, producing 9,339 lines / 159,245 bytes.
BreakpointType's constructor and GetValue no longer have VM register prologues.
Publication remains eight incomplete app records, two host-constructor refusals
at PC14 and nine metadata records. Method maps retain 195 entities / 9,745 spans.
Read-only review found no concrete correctness issue.

Tests, generated JS execution and RSS remain paused. Deferred regressions cover
Unknown fallback, target writes, constructor initialization, non-simple formals,
template/capture transport, tight total-output budgets and map rebasing. Whole
pasteboard source equivalence and declaration liveness in mixed bodies remain
unfinished.
