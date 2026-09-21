# Hole Loads and Guards

Reference: the pinned `OpenHarmony-v7.0-Release` identities in
`openharmony-v7.0-release.lock.json`.

## Runtime Evidence

In `ets_runtime/ecmascript/interpreter/interpreter-inl.cpp`, `LDHOLE`
loads the internal Hole sentinel, not a JavaScript value. The handlers
`THROW_UNDEFINEDIFHOLE_PREF_V8_V8` and
`THROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16` first test Hole identity.
The former reads its name register only on the Hole path; the latter
resolves its string-pool slot only on that path. Both preserve accumulator
and registers on normal completion. `RuntimeThrowUndefinedIfHole` in
`ecmascript/stubs/runtime_stubs-inl.h` concatenates the name with the
runtime's not-initialized message and creates a `REFERENCE_ERROR`.

## Typed Contract

`LiftHoleInstruction` produces `LoadHole` or `HoleGuard`, separate from
language scalar literals. The guard carries the tested accumulator/register
and a conditional name source: a register or a regional string-pool slot.
The slot is not a file offset and must use the enclosing method's pool.
Resolution must not be moved ahead of the guard during source emission.
The runtime requires a string name; this lift does not prove its type.

Lifting decoded instructions takes constant time and storage, performs no
runtime reads, and validates encoding widths and register bounds. Tests
cover both guard encodings, the distinct Hole load, malformed operands,
and register/pool boundaries. They verify the typed lift, not execution in
an Ark VM or historical semantic equivalence.

## Reconstruction Boundary

This is not yet a JavaScript TDZ implementation. Neither replacing Hole
with `undefined` nor unconditionally throwing for a lexical load is valid.
Binding identity, environment lifetime, initialization state, exceptional
control flow, and source declarations must be recovered before selecting
native `let`/`const` syntax. No Hole value is emitted as a scalar JavaScript
literal. The straight-line source emitter continues to reject these
instructions until their full semantics are integrated.
