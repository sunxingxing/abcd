# Captured initialization facts

## Actual failure boundary

After capture preservation, Home constructor 5851 passes the whole-constructor
origin proof and reaches the common printer. Its function creation at PC398
initially failed because builder 6322's shared AST rejected the initialized-super
check at PC17. Its captured read at PC15 was only conditionally Language. Debugger returns
are recorded in `build/pasteboard-next-blocker-debug.txt` and
`build/pasteboard-child-blocker-debug.txt`; these are ABC-only inspections, not
runtime equivalence tests.

## Value contract

`ValueKinds::Defined` denotes Language excluding undefined. It excludes Hole
and VM internal values because it is a refinement of Language, not a separate
VM atom. String and non-NaN-number imply Defined; their mixed phi join retains
Defined while dropping either narrower refinement. Joining unrestricted
Language drops Defined. Joining bottom preserves the other operand.

The shared AST can eliminate RequireInitialized only for Defined inputs or the
existing whole-constructor certificate. This adds no implicit premise to
captured reads. Ordinary captures remain Language and builder PC17 still
requires additional evidence. The sparse solver remains finite-height: a
scalar refinement weakens at most twice. Storage per fact is unchanged and
propagation remains edge-linear up to the constant lattice height and budget.

Do not infer Defined merely from a constructor opcode. The pinned
OpenHarmony-v7.0-Release `ecmascript/js_function.cpp`,
`JSFunction::ConstructInternal`, explicitly permits a derived result to be
undefined. `RuntimeStubs::RuntimeSuperCall` in `runtime_stubs-inl.h` returns that
result after abrupt-completion handling. The interpreter's native super fast
path also commits the native result. Therefore both decoded and typed
constructor-result rules stay conservative Language. Native JS constructor
expectations alone are not sufficient bytecode evidence.

## Template Transport

`CaptureDomain` is a typed use requirement separate from `CapturedSlot`
identity. `CapturedExpression`, `SourceCapture`, private stream tokens and
function wrappers preserve it. Rebinding to an outer capture preserves the
requirement; binding to a local cell currently rejects Defined because the
existing local certificate proves only Language. The legacy leaf binder and
public `BindSourceTemplate` likewise reject stronger requirements. Measuring or
discarding a template is not publication and does not discharge a requirement.

The common AST admits one conditional lowering: a captured load immediately
followed by RequireInitialized in the same normal block, with the guard's exact
SSA definition equal to that load. It upgrades that captured expression's
requirement to Defined and keeps a no-op PC anchor for the guard. It does not
admit copies, phi inputs, phased SSA, local reads or arbitrary preceding values.
The capture load is not eligible for private-copy DCE or current read fusion.
Thus even an otherwise unused load retains the obligation in the template.

There is no new graph walk: per-guard SSA definition lookup is constant time in
the existing normal adapter. Tokens grow by a typed field; their existing
`sizeof`-based admission charges the added storage. Inspection JSON exports
`required_domain` using enum values 0 (Language) and 1 (Defined).

## Next Integration

The next proof must derive Defined on the normal continuation of the actual
RequireInitialized guard, not retroactively on all uses of its SSA input.
It must preserve the exceptional input state, establish dominance over the
creation, resolve the same cell, and use transitive no-write evidence for all
subsequent possible mutations. A copied register snapshot and a later cell
reload are different obligations.

The child template now carries a typed Defined capture requirement through
rebasing and wrappers until its owner discharges it; a generic Language token
must not erase that requirement. Detached templates and unresolved parents
remain conditional or rejected. No copied source, source-map inference or
runtime ReferenceError construction is a substitute for this proof.

The parent-side stronger certificate is not yet implemented. This also
does not expand the existing range-super owner's premises: native-super versus
raw-VM exceptional timing still needs its own audit for malformed or unknown
parent constructors. No full Home completion or equivalence is claimed.

Both CLI targets build. The first value-domain-only survey in
`build/pasteboard-semantic-defined.jsonl` was unchanged. After transport, the
ABC-only survey `build/pasteboard-semantic-capture-domain.jsonl` reports four
additional conditional templates: methods 6322, 6486, 6650 and 6678. Template
count is 60; body fragments remain 113. The builder 6322 carries six Defined
capture tokens, including descendants' rebased requirements. The complete
record manifest remains unchanged; these are not four executable methods.
Tests and RSS tracking are paused. Deferred regressions cover lattice laws,
mixed phi joins, Language/undefined weakening, internal-value joins, before-guard
uses, exceptional paths, writes after creation and transitive rebasing.
