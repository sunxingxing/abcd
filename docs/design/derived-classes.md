# Derived Class Source Boundary

The pinned v7 `CALLRUNTIME_SUPERCALLFORWARDALLARGS` reads a constructor
register, the current frame's `new.target`, and all actual call arguments.
`THROW_IFSUPERNOTCORRECTCALL` mode 1 rejects an already-bound `this`; mode 0
rejects an unbound `this`; other encoded modes are runtime no-ops. Both checks
preserve the accumulator. Historical formats may decode, but source equivalence
needs a separate profile proof.

Dynamic 12.0.2.0 now enters class-heritage admission when the record
interpreter has captured an import or global parent snapshot at the DEFINE
site. The paired MultiCommunity `ViewPU` pages pass this profile gate. Their
constructors contain default-argument branches, a range super call, and
subsequent lifecycle effects: the original pure forwarding and exact
spread-field templates could not represent them. A later single-range-super
SSA proof admits that constructor shape, but unsupported members still prevent
whole-record source publication.

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

### Zero-field Rest-spread Constructors

The same proof now accepts zero trailing string-field writes. It still requires
the exact rest prologue, current constructor, rest-array provenance, single
super call, this guards/binding and terminal super-result return. Source
emission keeps explicit `constructor(...$rest) { super(...$rest); }`; it does
not omit the constructor or substitute a default native forwarding protocol.
The separate lifetime parent-brand requirement remains unchanged.

ABC-only debugger inspection of EntryBackupAbility constructor 5493 confirms
the proof returns an empty writes vector. Class emission then rejects the
actual `ParentRuntimeInvariant::kUnproven`, explaining the record's remaining
PC14 refusal. This is not an async method failure. The user explicitly rejected
the proposed narrowly scoped host-base-class premise on 2026-09-25.
EntryAbility and EntryBackupAbility must remain conservatively refused: neither
their API identity nor an import name licenses a lifetime ordinary-JSFunction
or non-Proxy prototype-chain assumption. No such premise is installed, and the
existing `ParentRuntimeInvariant::kUnproven` gate remains unchanged.
Both production CLIs build and read-only review
found no concrete defect in the zero-field extension. Tests, generated-JS
execution and RSS collection remain paused. Deferred regressions cover empty
field sequences with malformed rest/guards, extra effects, nonterminal returns,
wrong length, unknown parent brand and explicit-spread semantics.

### Range constructor control flow, increment 1 (2026-10-26)

Range-derived proofs now publish one certificate containing super sites, return
sites with receiver state, check coverage, and guard-to-construction bindings.
The previous single-site proof remains a sufficient fast path and produces the
same projection. Explicit spread, forwarding, and implicit constructors retain
their existing admission paths and parent-runtime requirements. This increment
adds no source-language premise.

The general proof uses the existing semantic instruction adapter, normal CFG,
register SSA, and dominators. An auxiliary SSA program observes a shadow receiver
binding and pending construction; it never changes the input body. Copy roots
and memoized phi equations compare actual guard operands to that binding.
Monotone normal-flow dataflow tracks unreachable/uninitialized/initialized/both
and whether a construction awaits binding. Each range construction must reach
its mode-1 check without intervening effects, throws, returns, or an initialized
pending cycle. The check constructs first, throws on an initialized path, and
binds on an uninitialized path. Alternative constructions may share a guard.
No construction is required to dominate returns or other constructions.

Mode-0 checks become native `this` reads, including their ReferenceError path.
Mode-1 checks map to a distinct native super binding phase after construction;
they are never treated as ignored, nonthrowing anchors. Native constructor exits
retain their own throwing phase. Object/function results return directly;
non-undefined primitives reach the VM/native TypeError check. Undefined results
require the bytecode receiver check/substitution, proved through SSA, edge
states, and dominating strict-undefined refinements. Source returns evaluate
their expression once. The ABI's uninitialized receiver transport is represented
as undefined; only certified receiver observations become native `this`.

The existing `EraseTransparentRethrowRegions` analysis remains the sole owner of
handler removal. Its proof admits the compiler's transparent implicit-super-check
handlers before semantic import. Remaining exception regions are refused; this
increment does not implement arbitrary catch/finally. Evaluation verification,
tree operands/flow, recovery flow, and fresh derived projection verification all
consume/check the complete site list. Recovery flow distinguishes construction,
binding, receiver-read, and constructor-exit exception points. Shared guards keep
all alternative native binding ports; consumers requiring one physical point
conservatively decline that optimization.

Let N be instructions/SSA nodes, E CFG/phi edges, and U indexed uses. Storage is
charged before admission; CFG/SSA/dominator builders receive the remaining
budget. Iterative worklists, copy compression, sorted indexes and memoized
relations target O((N+E+U) log N) work and linear retained storage. Relational
states/visits have an explicit input-sized resource bound; a denser product
returns typed resource exhaustion instead of starting quadratic per-site graph
walks. The algorithms reuse the project's SSA/dominator implementations and
standard monotone dataflow; there is no new dependency or licensing obligation.
Unproved receiver identity, return completion, order, lexical effects, handlers,
malformed instructions, and budget exhaustion retain typed refusals.

The O0 `derived_control` fixture and Node differential cover 57 outcomes,
including early returns, branch-local super sites, double construction before
ReferenceError, loop super calls, pre-super receiver reads, replacement objects,
user exceptions and alternate `new.target`. Analysis tests include shared-guard
phis, mode mutations and 200/201/400/800-site resource/scaling checks. Current
acceptance and baseline failures are recorded in source-recovery/status.md.
