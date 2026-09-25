# Stored Results in the Common Call AST

The unified call owner must represent its result destination before nested
call-as-argument recovery can consume that destination. CallResultStore keeps
the private register and original save instruction ID, not a printed binding.
FoldStoredCalls runs after return recovery and before receiver/argument folding.
It inspects each staged owner, its indexed invocation and the adjacent save;
there is no per-owner interval scan, recursion or scratch allocation.

Admission requires normal SSA, no catches, one block containing the property
owner, invocation and save, an unmerged NoOperation invocation anchor, and an
unmerged stored SourceAssignment saving the accumulator to a private register.
The original instruction must be that same pure, normally completing copy.
The invocation's accumulator SSA definition must have exactly one use, which
must be the save's sole read. Phi and other observers therefore prevent folding.
Existing returned or retargeted calls are excluded.

Only result transport changes: invocation still follows the original property
Reference and argument interval. The register commits after normal completion;
throws do not commit it. The printer uses Target for the destination, preserving
slot declaration and ABI-binding checks. The removed save retains an adjacent
previous_origin link to the invocation, and printing validates that coordinate
and its NoOperation anchor. A stored-result certificate is incompatible with
returned, unused or argument-only call results at this stage. Result liveness
and return folding must not reinterpret the owner's getter SSA as invocation SSA.

This builds on the existing call interval and sparse SSA machinery; it adds no
dependency or execution premise. Indexed queries give O(I log(I + U)) work for
I instructions and U SSA reads, constant scratch. Inline certificate storage
is charged through sizeof(SourceStatement). Nested call argument traversal and
native argument-only emission remain the next integration step; this change
alone must not claim to eliminate IIFEs.

Tests, generated-JS execution and RSS remain paused. Deferred regressions cover
extra/phi result uses, catches/phased SSA, cross-block saves, non-copy stores,
argument side effects and throws, target/receiver register aliases, nested call
owners, conflicting result certificates, malformed anchors and slot census.

## Production Evidence

Both production tools built successfully. The first export destination,
build/pasteboard-stored-calls-01, already contained an older output and was
rejected with kDestination; it is not evidence for this change. The fresh
ABC-only export build/pasteboard-call-result-stores-20260925 succeeded.

Across the six pages, 22 result transports are folded. Total source becomes
4,236 lines / 90,661 bytes, down 22 lines / 286 bytes from
pasteboard-property-receivers-01. PageA method 7559 now writes
`$r6 = this.id__();` inside the still-retained argument IIFE. Its property PC 66,
invocation PC 74 and save PC 77 all map to bytes [4055, 4074), that assignment
including newline. The createData call also writes its result register directly.

Partial maps retain 195 methods / 9,745 spans. Statuses remain eight incomplete,
two host constructors refused at PC 14 and nine metadata-only records.
Read-only review found no concrete defect. No tests, generated JS execution or
RSS checks ran; full equivalence and memory compliance remain unproven.
