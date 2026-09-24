# Derived Class Source Boundary

The pinned v7 `CALLRUNTIME_SUPERCALLFORWARDALLARGS` reads a constructor
register, the current frame's `new.target`, and all actual call arguments.
`THROW_IFSUPERNOTCORRECTCALL` mode 1 rejects an already-bound `this`; mode 0
rejects an unbound `this`; other encoded modes are runtime no-ops. Both checks
preserve the accumulator. Historical formats may decode, but source equivalence
needs a separate profile proof.

Dynamic 12.0.2.0 now enters class-heritage admission when the record
interpreter has captured an import or global parent snapshot at the DEFINE
site. The paired MultiCommunity `ViewPU` pages pass this profile gate, then
refuse the constructor proof: their constructors contain default-argument
branches, a range super call, and subsequent lifecycle effects. The current
pure forwarding and exact spread-field templates cannot represent that body;
no page source is published on this basis.

`ProveForwardingDerivedConstructor` accepts two straight-line templates: private
copies of ABI inputs, one super-forward-all or positional `SUPERCALLTHISRANGE`
call through the current constructor,
a mode-1 check of the prior uninitialized `this`, copying the super result to a
private local, a mode-0 check of that result, and a final return of it. It
rejects ABI-input writes, branches, exception regions, other instructions,
wrong provenance, repeated calls/checks, and missing returns. Provenance is an
indexed typed origin per register plus accumulator; complexity is
O(instructions + registers), including total positional arguments, with an
explicit register-state byte budget. The narrow range form retains its inline
cache slot as format evidence, never a source argument; narrow/wide operand
widths and register windows are checked against the pinned v7 ISA and
interpreter handler. Unit tests cover malformed wire forms, false proofs,
resource refusal and 200/201/400/800-instruction inputs. The
actual Immersive `EntryAbility` and `EntryBackupAbility` constructors both pass.

The source writer now admits a pinned v7 imported-base or global-base class
when the record interpreter proves the parent register holds a snapshot from
a guarded import or `TRYLDGLOBALBYNAME`, the observable constructor length is
zero, the constructor passes this proof, and every class member is fully
sourceable. It emits `extends $loadedN` or `extends $globalN` at the DEFINE
site, preserving parent evaluation and exception order. A forward-all proof
retains `constructor() { super(...arguments); }`; a positional proof requires
observable class length to equal the canonical ABI parameter count and emits
an ordered `constructor($p0, $p1) { super($p0, $p1); }` (using validated debug
parameter names when present). Node oracles cover argument and
`new.target` forwarding, prototype chain, length, and a later import read.
Initial method kind alone still does not prove base/derived heritage. Other
parent origins, constructors, static initializers/accessors after creation,
and unsupported member bodies remain whole-record refusals.
On the real Immersive sample, both `EntryAbility` and `EntryBackupAbility`
pass their import-parent and constructor checks. `EntryAbility` still stops
at class-member source admission (`kClass` at entry PC 14). The Shopping
entry's guarded global call is followed by a global parent read and class
creation; the record interpreter now reaches class admission instead of
refusing the global read. Its 72-instruction constructor has branches,
property writes and a range super call followed by user code, so the pure
positional template still refuses it. This is not whole-record source success.

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

The same pinned v7 async enter/resolve/reject handlers also execute admitted
dynamic 12.0.6.0 bytecode. Historical async ordinary class members are no
longer refused solely by container profile: each method must still pass the
exact `ProveAsyncCompletion` no-await/catch-all proof and normal source-body
checks. The real pasteboard `EntryBackupAbility` members 5437 and 5465 pass
and produce executable async methods under their own module table. Older
dynamic 11.0.2.0 remains refused. This does not publish the pasteboard record:
its constructor uses `SUPERCALLSPREAD`, whose JSFunction-brand runtime
contract is not established by an import snapshot.

The historical pasteboard `EntryAbility` constructor (entity 5083, dynamic
12.0.6.0) is a different template: the exact ABI-copy/rest prologue, one
`SUPERCALLSPREAD` through the current constructor, an uninitialized-this
check, a bind of the super result, initialized-this checks, ordered
`STOBJBYNAME` writes of string constants, and return of the super result.
`ProveSpreadFieldDerivedConstructor` tracks typed register/accumulator origins
and field pool indices in O(instructions + registers + writes) time and
bounded storage. It rejects control flow, exception regions, extra effects,
wrong receiver/value provenance and modified rest arguments. The class writer
emits an observable-length-zero `constructor(...$rest)` with native
`super(...$rest)` and strict `this["name"] = "value"` writes, **not** class
field declarations. Constant names and values are read from the same ABC
regional pool; indexed fast-path names remain refused. Both dynamic 12.0.6.0
and 13.0.1.0 are covered by a synthetic class/Node oracle, and the real
pasteboard constructor is checked without sidecar source as an engine input.
The class writer requires a separate trusted
`ParentRuntimeInvariant::kAlwaysJsFunction` witness for this spread template.
A bare import/global snapshot or one-time brand observation is insufficient:
the superclass is re-read when each constructor call executes and may have
been replaced via `Object.setPrototypeOf`. Pinned `RuntimeSuperCallSpread`
rejects a constructible Proxy superclass while native `super(...rest)` invokes
it. No corpus caller currently supplies the lifetime witness, so the real
record remains
unpublished. With a trusted witness, its next class-member obstacle is an
exception-region environment-flow proof. This proof does not claim all
derived constructors.
