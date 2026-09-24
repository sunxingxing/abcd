# Captured initialization facts

## Actual failure boundary

After capture preservation, Home constructor 5851 passes the whole-constructor
origin proof and reaches the common printer. Its function creation at PC398
fails because builder 6322's shared AST rejects the initialized-super check at
PC17. Its captured read at PC15 is only conditionally Language. Debugger returns
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

## Next Integration

The next proof must derive Defined on the normal continuation of the actual
RequireInitialized guard, not retroactively on all uses of its SSA input.
It must preserve the exceptional input state, establish dominance over the
creation, resolve the same cell, and use transitive no-write evidence for all
subsequent possible mutations. A copied register snapshot and a later cell
reload are different obligations.

The child template must carry a typed Defined capture requirement through
rebasing and wrappers until its owner discharges it; a generic Language token
must not erase that requirement. Detached templates and unresolved parents
remain conditional or rejected. No copied source, source-map inference or
runtime ReferenceError construction is a substitute for this proof.

This contract does not yet implement that interprocedural transport. It also
does not expand the existing range-super owner's premises: native-super versus
raw-VM exceptional timing still needs its own audit for malformed or unknown
parent constructors. No full Home completion or equivalence is claimed.

Both CLI targets build. The ABC-only method survey in
`build/pasteboard-semantic-defined.jsonl` is unchanged from the preceding
closure-domain survey; no increased pasteboard coverage is claimed.
Tests and RSS tracking are paused. Deferred regressions cover lattice laws,
mixed phi joins, Language/undefined weakening, internal-value joins, before-guard
uses, exceptional paths, writes after creation and transitive rebasing.
