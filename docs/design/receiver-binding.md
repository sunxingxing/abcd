# Native Receiver Binding

## Common Pipeline

The common AST carries an optional register-to-native-this binding. The common
printer consumes it through the existing typed ScalarNames binding and excludes
that slot from emitted declarations. Ordinary functions retain their existing
native receiver handling. A derived constructor supplies an additional producer
proof, not a separate body writer: independently validated SuperRange initializes
the receiver, and its returned object is the native this value even when the base
constructor returns a replacement object. This introduces no superclass-brand
or prototype-stability assumption.

## Proof And Boundaries

RecoverReceiverBinding runs after retargeting and final accumulator cleanup.
The super call must be stored to a register and retain its original range.
Existing CopyOrigins supplies original SSA copy identity without crossing phis
or method entry. All observed values of the target must have that identity.
Phis and unrelated non-erased definitions reject the binding. The exhaustive
VisitRegisterCommits audit additionally rejects any other final AST commit to
the register, including result-owning protocols. The bound-read audit excludes
operations that cannot print all register reads through typed bindings.

One dead pre-initialization transport is allowed: a pure private copy already
erased by DCE, with zero remaining uses of its output, immediately followed by
an erased RequireUninitialized check whose sole original SSA read is that
output. This is not permission to treat arbitrary empty AST nodes as dead
reads. Fused and result-owning copies remain excluded.

The stored super result becomes unused, but the super expression still executes
at its original position. Statement origins and retained copy anchors remain;
PC mappings are not reconstructed from emitted text. Catch regions, phased SSA,
argument regions, reused receiver slots and cross-closure captures are outside
this proof. A mutable lexical cell is not interchangeable with native this.

This is standard SSA copy-identity propagation followed by an emitted-write
audit, using existing project analyses with no new dependency. For I original
instructions, B AST statements, V SSA values and U reads, traversal is
O(I+B+V+U), plus existing indexed SSA queries and one O(log I) producer lookup.
The pass allocates no auxiliary collections; its optional binding adds fixed
inline storage to SourceAst. Measurements remain paused; this does not
establish process memory compliance.

## Evidence And Deferred Verification

ABC-only PasteBoard export removes its stable receiver declaration and emits
native super, this member accesses and return this. Home's lexical receiver and
reused register remain unchanged. Receiver snapshots used by later stores still
need value-level local recovery; this step does not claim full temporary cleanup.

Both production CLI targets build. The export retains 195 mapped methods and
9,745 position entries; eight sources remain incomplete, two records refused
and nine metadata-only. Read-only review found no concrete defect. Tests,
generated-JS execution and RSS tracking remain paused at the user's request.
Deferred regressions include base-returned objects, pre-super reads, repeated
super, slot reuse, phi/loop inputs, fused owners, ordinary/arrow captures, catch
boundaries, source coordinates and 200/201 scaling.
