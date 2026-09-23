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

The source writer now admits a pinned v7 imported-base or global-base class
when the record interpreter proves the parent register holds a snapshot from
a guarded import or `TRYLDGLOBALBYNAME`, the observable constructor length is
zero, the constructor passes this proof, and every class member is fully
sourceable. It emits `extends $loadedN` or `extends $globalN` at the DEFINE
site, preserving parent evaluation and exception order, and uses
`constructor() { super(...arguments); }`. Node oracles cover argument and
`new.target` forwarding, prototype chain, length, and a later import read.
Initial method kind alone still does not prove base/derived heritage. Other
parent origins, constructors, static initializers/accessors after creation,
and unsupported member bodies remain whole-record refusals.
On the real Immersive sample, both `EntryAbility` and `EntryBackupAbility`
pass their import-parent and constructor checks. `EntryAbility` still stops
at class-member source admission (`kClass` at entry PC 14). The Shopping
entry's guarded global call is followed by a global parent read and class
creation; the record interpreter now reaches class admission instead of
refusing the global read. This is not whole-record source success.

For pinned v7 async ordinary class members, `ProveAsyncCompletion` recognizes
the compiler's no-await template: private `ASYNCFUNCTIONENTER` result, one
catch-all region covering the entire normal body, a matching resolve/return,
and an exact reject/return handler. The promise register must be a private
local and unread by the normal body. The native `async` method provides the
same promise assimilation and implicit exception rejection; the internal
promise operations and handler are removed before normal body analysis. This
is a linear, bounded transformation with no branch relocation. Await/resume,
partial handlers, and historical semantics remain refused. The v7 interpreter
handlers for `ASYNCFUNCTIONENTER`, `ASYNCFUNCTIONRESOLVE_V8`, and
`ASYNCFUNCTIONREJECT_V8` are the semantic reference. Node tests check async
method identity, promise adoption, and rejection-safe publication.

The real Immersive `EntryBackupAbility.onBackup` body (method entity 9488) now
passes member emission. Its three-argument named call uses the existing
getter-before-argument IIFE proof extended from `CALLTHIS1` to `CALLTHIS3`;
the source oracle checks receiver, argument order, and discarded return.
`onRestore` (entity 9516) also has a four-argument `CALLTHISRANGE` after a
nested `JSON.stringify`. The range call and one pure-copy nonterminal named
call are now proven. `EntryBackupAbility` publishes one `.mjs` file, and the
complete-record count is 5/45. A VM module oracle links synthetic platform
imports and executes both async methods, including getter order, receivers,
argument lists, constructor forwarding and rejection identity.
