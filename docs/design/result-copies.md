# Saved Result Copies

FoldResultCopies retargets a stored register result through its sole remaining
private copy consumer. It uses typed AST and original SSA, not constructor names
or generated source matching. This lets native calls, including range super,
commit directly to the final local without introducing another body writer.

Both nodes remain in one normal block. The producer must be a stored register
assignment accepted by CanRetargetExpression, without object-field or literal
ownership. Its original direct or retargeted saved SSA definition must have one
remaining use, precisely the consumer's original register read. The consumer
must remain a plain private register copy to a distinct target, with its direct
or retargeted commit validated by PrivateCopyOutput. Phased/catch methods are
excluded and all phi/exception observers participate in the use count.

Every intervening source node must already be empty and free of unrelated
origin ownership. Original effects must be absent, except typed SuperCheck
operations already discharged by the AST's initialization proofs. No observable
operation or emitted private read is crossed. The producer RHS executes at its
original point and commits the final target only on normal completion. No
class field write, allocation or getter moves or disappears. The consumer's
old target value is not read between these points; RHS reads still precede the
new commit. This does not create a native-this alias or relax super admission.

The removed interval retains each PC and forms a previous-origin chain to the
producer expression's span. The consumer's already attached save anchor follows
that chain. Candidates are cleared after a successful retarget because its new
target no longer denotes the producer's original saved SSA definition. Further
retargeting would need a new certificate, not reuse of the original use counts.

This pass precedes ElideKnownReloads: no later physical accumulator fact may
depend on a commit that retargeting removes. Admission and origin updates scan
disjoint candidate intervals. There is no allocation or per-variable graph
walk; cost is O(I log R) using the normal SSA read index (I instructions,
R reads), with constant scratch. Existing byte/work admission remains in force.

Tests, JS execution and RSS remain paused. Deferred regressions include throwing
producers, destination-as-RHS input, register reuse, explicit result observers,
phis, empty guard chains, fused origins, literal/class ownership, direct and
retargeted copy forms, multiple candidates and exact PC positions.

## Production Evidence

Both production CLI targets built. ABC-only export
`build/pasteboard-result-copies-20260925` removes one super-result transport in
each of PageA, PageB, PasteBoard, ProgressBar and ShareDitect; Home is unchanged.
These constructors now emit `$r2 = super(parent, __localStorage, $r6, extraInfo)`.
Output totals are 3,847 lines / 86,108 bytes. PageA method 7201 PCs
79/83/85/87/90/92 share the retained assignment's [2483,2536) span. Argument
positions remain individually emitted. Partial maps retain 195 methods / 9,745
spans. Statuses remain eight incomplete records, two constructors refused at
PC 14 and nine metadata-only records. Read-only review found no concrete defect.
Tests, generated JS execution and RSS tracking were not run; full source
equivalence and record completion remain unproved.
