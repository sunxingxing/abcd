# Derived Class Source Boundary

The pinned v7 `CALLRUNTIME_SUPERCALLFORWARDALLARGS` reads a constructor
register, the current frame's `new.target`, and all actual call arguments.
`THROW_IFSUPERNOTCORRECTCALL` mode 1 rejects an already-bound `this`; mode 0
rejects an unbound `this`; other encoded modes are runtime no-ops. Both checks
preserve the accumulator. Historical formats may decode, but source equivalence
needs a separate profile proof.

`ProveForwardingDerivedConstructor` accepts one straight-line template: private
copies of ABI inputs, one super-forward call through the current constructor,
a mode-1 check of the prior uninitialized `this`, copying the super result to a
private local, a mode-0 check of that result, and a final return of it. It
rejects ABI-input writes, branches, exception regions, other instructions,
wrong provenance, repeated calls/checks, and missing returns. Provenance is an
enum per register plus accumulator; complexity is O(instructions + registers)
with an explicit register-state byte budget. Unit tests cover malformed wire
forms, false proofs, resource refusal and 200/201-instruction inputs. The
actual Immersive `EntryAbility` and `EntryBackupAbility` constructors both pass.

The source writer now admits a pinned v7 imported-base class when the record
interpreter proves the parent register holds a guarded import snapshot, the
observable constructor length is zero, the constructor passes this proof, and
every class member is fully sourceable. It emits `extends $loadedN` at the
DEFINE site, preserving parent evaluation and exception order, and uses
`constructor() { super(...arguments); }`. Node oracles cover argument and
`new.target` forwarding, prototype chain, length, and a later import read.
Initial method kind alone still does not prove base/derived heritage. Other
parent origins, constructors, static initializers/accessors after creation,
and unsupported member bodies remain whole-record refusals.
On the real Immersive sample, `EntryAbility` and `EntryBackupAbility` now pass
their import-parent and constructor checks but stop at class-member source
admission (`kClass` at entry PC 14). Complete executable records remain 4/45;
the synthetic Node oracle is not evidence that those real methods are ready.
