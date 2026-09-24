# Class creation in semantic IR

`ClassCreation` and `DefineAccessor` now enter the same `SemanticOperation`
model as ordinary function creation. The existing validated decoders retain
the constructor/literal pool slots, encoded length, cache slot, parent register
and accessor operand registers. `SemanticInstruction::pc` retains provenance;
no source-map input or name-based dispatch is involved.

## Runtime contract

Reference: pinned OpenHarmony-v7.0-Release `ets_runtime`:

- `interpreter/interpreter-inl.cpp`, class handlers at 5178-5220:
  read parent register and current frame environment/module; replace the
  accumulator only after non-abrupt class creation.
- `stubs/runtime_stubs-inl.h`, `RuntimeCreateClassWithBuffer` and
  `RuntimeSetClassInheritanceRelationship`: capture environment and module,
  allocate class/members, inspect heritage and potentially call a parent
  `prototype` getter. Hole means absent heritage; null means `extends null`.
- `RuntimeDefineGetterSetterByValue` at 2708: convert the key, optionally name
  accessor functions, update function kind and define a property. The input
  accumulator's ToBoolean controls naming, not enumerability. Enumerability
  depends on class ownership. Normal result is the object operand.

Both operations conservatively include heap reads/writes, user code and
allocation in normal/before-throw effects. Class creation additionally reads
environment/module state. Both may throw. Existing typed register-effect
lifting supplies all input uses and the normal accumulator definition to SSA.
Normal-result value rules remain Language, not a new object/heritage proof.
Decoded and semantic value-flow entry points agree on these rules.

## Proof boundaries

Class creation is a capture barrier in normal local-cell SSA and phased local
initialization. Transitive capture-preservation inspection refuses a nested
class until its constructor and literal-method dependency graph is resolved;
merely lifting a class must not certify a containing function as read-only.

This does not license native class syntax, accessor descriptors or treating
Hole as a source value. The generic input verifier still rejects unproven
internal values, and the source AST still refuses these operations without
ownership/heritage certificates. The existing record/class writer remains a
separate retained path. Completion requires those certificates, class AST
nodes and shared printing, including PC-to-generated-position propagation.

No new graph solver or per-variable traversal is introduced. Instruction
lifting is constant-sized; existing CFG/SSA work and memory budgets apply.
The new variant alternatives may affect structure size, which existing
sizeof-based admission accounts for. Tests and RSS measurement are paused;
no performance or source-equivalence claim follows from CLI compilation.

Deferred regressions: both class operand widths and register bounds, normal
versus exceptional accumulator state, throwing heritage/key conversions,
class capture barriers, nested class writers, accessor naming/enumerability,
Hole versus null heritage and fail-closed AST admission.

## Pasteboard checkpoint

ABC-only inspection of ProgressBar entry 9915 now builds the complete normal
semantic method: 156 instructions, 11 blocks and 157 SSA values, recorded in
`build/pasteboard-entry-class-ir.json`. Previously it rejected class creation
at PC264. The instruction inspection at
`build/pasteboard-entry-class-ir-instructions.jsonl` retains the class operands
and accessor definitions at PCs303, 326, 349 and 372 with exception edges.
Native source validation now reaches its independent local-environment gate
at PC130 (`SemanticProofError::kInternalValue`); it does not emit a class AST
or complete record. Both CLI targets built and read-only review found no
specific issue; no tests or RSS tracking were run.

### Initialization across the prelude join

The local initialization proof now accepts forward CFGs where environment
allocation dominates the first initializing store in a different block.
The store must still dominate every admitted subsequent read/capture, and
every store must pass Language-value verification. Cycles and closure-write
conditions are unchanged. Allocation-block lookup uses a quota-charged binary
search: O(S log B) additional work for S candidate stores and B blocks, with
no additional allocated storage or per-cell graph walk.

In entry 9915, debugger inspection at PC130 now reports `initialized=true`,
store instruction 47 (PC128), and dominance interval [4,10). The independent
unknown-closure-write check still rejects at PC130: creation instruction 32
has no capture-preservation premise at this standalone verifier entry. This
is an initialization-proof improvement, not a newly emitted method. Connect
actual child capture summaries next; do not substitute a blanket Language
assumption. CLI builds and read-only review completed; diamond/cycle/budget
regressions remain deferred while tests are paused.

### Creation-site preservation in normal verification

`VerifySemanticSource` now accepts optional sorted `CapturePreservation`
certificates from the unchanged file/profile/body. The local proof merges
these PCs with instruction order, validates that each names a capture-capable
operation, and removes only the matching barrier. Duplicate, unsorted,
non-creation and unconsumed sites refuse admission. Literal object/array
creation is also a barrier because its buffer may install captured methods.
Neither initialization nor store-domain checks are weakened. The merge adds
O(I + C) quota-charged work and no new storage.

The existing capture-summary API is now a public source-layer header, shared
by production owners and the single-method source CLI. The CLI builds actual
child summaries, charges their live storage/work alongside IR and lookup,
and retains `ClosureWrites::kUnknown`; no blanket premise is introduced.
It also accounts for retained proof, region and named-scope allocations when
admitting subsequent stages.

ABC-only ProgressBar inspection with `--function-source` now passes PC130
and stops at PC200 (`kInternalValue`), the saved result of multiplication at
PC197. This is the next register-value-flow obligation, not a class-source
success. CLI compilation and read-only review completed; tests/RSS remain
paused. Certificate rejection, mixed proven/unproven creators, buffer
barriers and low-budget cases still require regressions when testing resumes.

### Typed class emission boundary

`WriteCreatedClass` now has an overload accepting the already validated
`ClassCreation` and its original PC directly. The decoded-instruction entry
delegates to it, preserving the existing constructor/member, owner, static
field, heritage and atomic-publication checks. A future AST consumer does not
need to manufacture bytecode to reuse this emitter.

`BoundClassParent` admits an owner-proven initialized local snapshot binding,
not arbitrary source text or an import/global-only ordinal. Its default
parent runtime invariant remains unproven. Invalid identifiers and collisions
with the inner class name, ClassValue or reserved MethodSelf namespace refuse
emission, avoiding heritage evaluation in an unintended TDZ.

This adapter is not yet class AST support. Before enabling nested class
emission, thread the existing bounded `CreationExpansion` context through
class constructors and members, including recursive dependencies. The current
class body owner otherwise creates a new child context, which would not bound
newly admitted class recursion. Accessor/attached-method assembly proofs and
outer capture binding also remain required. CLI compilation succeeds; tests
and RSS checks are still paused. No additional record completion is claimed.

### Shared AST and bounded class expansion

`ClassExpression` now retains the typed creation and original PC in the shared
source AST. The printer passes the verified parent register snapshot to the
typed class emitter, streams output through its position cursor, and preserves
the existing constructor/member/heritage checks. The creation span is mapped;
nested method PC maps are not yet merged. This is an adapter to the retained
class emitter, not proof that every class body uses the common AST.

Class constructors, methods and accessors share the caller's synchronous
`CreationExpansion`. Each method enters before decoding and keeps its scratch
grant live through proof and printing. Ancestor cycles are rejected; depth,
scratch and work admission remain bounded. Dry and real emission use the same
context, and failed/preflight work is not refunded. Resource errors, including
nested body output exhaustion, retain resource classification in the common
printer. This does not implement the deferred asynchronous scheduler or prove
the process-wide memory cap.

The stream deliberately does not accept capture tokens: unresolved outer
captures still reject emission. Hole heritage and subsequent accessor/method
assembly remain unsupported here. No empty substitute class is emitted.

ABC-only inspection of entry 9915 now passes class AST construction at PC264
and rejects `DEFINEMETHOD` at PC295. Because AST construction fails before
printing, this inspection does not demonstrate successful class emission.
The record inspection at `build/pasteboard-source-class-expansion/manifest.json`
still has two complete application records. PageA, PageB and ShareDitect now
reject at the earlier class boundary PC194, rather than the later function
boundary PC196; no coverage improvement is claimed. The unchanged legacy
ProgressBar record path still rejects at PC120.

Both production CLI targets compile and the change received read-only review.
Tests and RSS tracking remain paused. Deferred regressions additionally include
nested class cycles, sibling/preflight work exhaustion, nested output-limit
classification, unresolved captures, and creation versus child position maps.

### Method and accessor AST operands

The shared AST now retains `MethodExpression` separately from ordinary
`FunctionExpression`. It carries the complete creation operands, instruction
coordinate and accumulator home-object input. Function/method input forms are
checked against their typed creation kind, and both advance the capture barrier
used by subsequent ordinary closures. `AccessorExpression` retains the four
register operands and accumulator naming flag; its normal result overwrites
the accumulator. The enclosing statement retains the original PC.

These are unresolved class-assembly nodes, not licenses to emit ordinary
functions or calls through `Object.defineProperty`. The printer explicitly
returns `SourceError::kClassAssembly` for either node until an owner proves
their assembly. Pinned `ObjectFactory::NewJSFunctionForDefineMethod` selects
the no-prototype/async/generator function class and sets home object;
`RuntimeDefineGetterSetterByValue` additionally converts the key, changes
accessor kinds/names and installs class-dependent descriptors. None of those
effects may be discarded by introducing source syntax.

ABC-only entry 9915 inspection now passes methods/accessors at PCs295-379
and stops at the own-property method installation at PC384. It still produces
no function source. Both production CLIs build and read-only review found no
specific issue; no tests, JS execution or RSS measurements were run.

Next, assembly must resolve SSA identities from class creation through
prototype reads and method home objects to installation. Folding into native
class syntax requires unescaped identities and an effect-order proof, including
throwing key/descriptor operations and closure environment changes. Retain all
participating PCs; do not merely delete the intervening instructions, identify
members by source strings, or infer class ownership from an arbitrary property
named `prototype`. Constructor/member role and capture checks remain mandatory.
