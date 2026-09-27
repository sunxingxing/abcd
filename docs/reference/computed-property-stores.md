# Computed Property Stores

Pinned reference: OpenHarmony-v7.0-Release, identities in the reference lock.
`interpreter-inl.cpp` STOBJBYVALUE_IMM8/IMM16 reads object/key registers and
accumulator RHS, saves/restores acc around the IC/fast/slow paths. The slow
`RuntimeStObjByValue` in `runtime_stubs-inl.h` checks object coercibility,
converts the key, then calls SetProperty with strict=true.

The existing semantic `ValueWrite` now imports as `ValuePropertyWrite` with
base/key/stored-value uses and no result. Effects and original-PC provenance
remain those of the original instruction. It is never a removable pure op.
Own definition, super/private access and indexed runtime operations are not
admitted by this mapping.

The common expression forest emits an AssignmentExpr with a ComputedKey.
The source must be strict; the independent operand checker rejects sloppy
trees. A typed ReferenceBoundary in EvaluationShape requires RHS producers
to remain materialized: an original RHS call completed before key conversion,
whereas inlining it into `base[key] = rhs()` would reverse that ordering.
The restriction also applies to pure parent expressions containing getters.
This initial policy is conservative even for provably pure RHS producers;
later relaxation needs an independent proof, not a printer heuristic.

Tests include narrow/wide import and acc preservation; independent rejection of
forged RHS inlining, swapped base/key and sloppy mode; encoded creation/body
through PreparedFamily/FamilyEmission and an executable JS oracle. A store's
original PC maps directly to its generated expression interval. Integration
cases compare traces, exception identity/type, final state and receiver/value
identity. They do not compare engine-specific TypeError text and are not a
pinned Ark VM differential execution. No whole-corpus completion follows.
