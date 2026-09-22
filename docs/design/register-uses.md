# Register Def/Use Index

Connect decoded methods to live-in and phi placement without trusting ISA
may-write flags as definite normal-path definitions. `LiftRegisterEffect`
reuses the existing typed scalar, branch, lexical, Hole, property, call and
function-creation lifts. Reads are conservative may-reads (including a Hole
diagnostic name register); a destination is written only on normal completion.
No result is asserted to be a language value. Environment/Hole values remain
distinct concerns for the later value-flow analysis.

Fixed reads use a bounded array; ordinary range-call arguments stay a typed
window until method indexing. Lexical stores read their real value source:
modern acc versus the deprecated explicit register. Stores preserve acc.
Unknown instructions fail rather than borrowing guessed effects from ISA bits.
The caller provides validated decode output, as required by the constituent
lifts. This is neither complete heap effects nor source-lowering certification.

`RegisterUses::Build` takes the unchanged validated method and its normal CFG,
frame size, a shared `IdfQuota`, and a byte budget. Reject exceptions/resumes and
invalid graph shape. Discover normal reachability once, then process each
reachable instruction once. Expand only actual reads, charge them, and collect
typed (slot, block, instruction, read/write) events. Sort by slot/block/position,
with reads preceding a write at the same instruction. Summarize each block into
defining/upward-use sets for each referenced slot; absent frame slots consume no
storage. Accumulator and virtual registers have distinct typed identities.

The quota charges validation, reachability, instruction/access visits, each
sort comparison, grouping and output copies. Failure during sorting destroys
scratch and returns a typed failure, never a partially indexed method. The
input body's and graph's retained memory is outside this operation's byte
budget and must remain admitted by the method owner.

The owned result has sorted binding descriptors and flat block arrays. It
survives source/body/graph destruction and returns no pointers into scratch.
Lookup is logarithmic; retrieving a binding's block lists is constant-time.
Build costs O(B+E+I+U log U), U the expanded register accesses. PMR counts growth
overlap plus final arrays while scratch remains alive. Reuse the same method
quota for extraction, liveness and placement; errors retain work consumed and
publish no partial result. Keep the owned result's bytes admitted while later
queries run. No frame-sized or binding-times-block matrix is allocated.

## Verification Sequence

1. Add failing instruction-effect and decoded-method index tests. Verify a
   diamond writing r1 on both arms produces r1's live-in/phi at the join, while
   acc is overwritten before its join read and needs no phi there.
2. Add effect extraction, typed slot ordering, and a bounded method index using
   existing local allocation/quota conventions. Tests must include legacy
   lexical store inputs, conditional Hole names, implicit range arguments,
   self-copy read-before-write and ignored ISA may-access flags.
3. Exercise loops, unreachable unsupported instructions, error atomicity,
   resource/work limits, and 200/201/geometric scaling without frame scans.
4. Run Debug and sanitizer tests, request independent review, record results
   and commit. No source-coverage increase or whole-record recovery is implied.
