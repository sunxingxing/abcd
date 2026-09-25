# Consumed Staged Receiver Uses

BuildSourceAst already proves the explicit receiver and property-read object
have the same value (directly or through one verified copy). SourceStagedCall
then retains the native property Reference across parameter evaluation. Its
printer consumes the property object's accumulator and argument operands, but
does not read the VM call's explicit receiver register.

For normal SSA only, creating this node now decrements exactly one use of the
receiver's SSA value. Zero before decrement is an invalid-effect failure. This
does not deduct a property-object read, argument read, or phi input. If the
same receiver register also supplies an argument, that independent use remains.
Phased exception routes retain original counts pending their separate audit.

The existing sparse ElideDeadAssignments pass can consequently remove private
receiver copies whose last emitted use disappeared. It still rejects effectful
producers and fused replacement nodes. Source statements remain as zero-width
PC anchors. Later folds consume the adjusted count, whose reduction is backed
by a completed typed AST transformation, not guessed from printed source.

Adjustment costs O(1) per staged call and allocates nothing. Sparse DCE retains
its admitted queue and existing complexity. No suffix scan or new per-register
liveness traversal is introduced.

## Production Evidence

Production targets abc_semantic and abc_structure built successfully. ABC-only
pasteboard export `build/pasteboard-receiver-uses-01` totals 8,118 lines /
143,524 bytes, down from 8,396 / 146,649. Seven records changed; CommonConstants
is unchanged. Logger logging methods no longer save the receiver in a register.

Coverage remains eight incomplete source records, two refused constructors at
PC 14 and nine metadata records. Partial maps retain 195 methods / 9,745 spans.
Independent read-only review found no concrete correctness issue. Tests,
generated JS execution and RSS checks remain paused. Deferred regressions
include receiver/argument aliases, repeated arguments, property-object copies,
additional normal/phi uses, nested calls, phased fallback, sparse-copy chains
and zero-width receiver-save maps. Export counts do not prove equivalence.
