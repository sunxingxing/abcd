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

This proof alone does not admit a derived class expression. Source integration
still needs a parent value proven at the class-creation site (the current
samples use a guarded import snapshot), an observable constructor length and
parameter ABI matching the generated `super(...arguments)` form, and complete
member-body recovery. Initial method kind does not prove base/derived heritage.
Until these contracts are connected, record source continues to refuse those
sites instead of emitting an incorrect `extends` or executable body.
