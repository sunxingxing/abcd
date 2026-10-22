# ISA round 10: separate script entry and global scope

**Round 11 update:** the user approved the entry accessor difference on
2026-10-22 under V9. The temporary gate is removed and all 18 script fixtures
complete; earlier round-10 results below remain historical evidence. See
[isa-misc-constructors.md](isa-misc-constructors.md) for current acceptance.


This is a partial implementation of the user-approved GlobalScripts predicate
(2026-10-22), not an expansion of module routing. V9 adds the predicate; historical
contracts remain unchanged. Global lexical/class declarations, checked name
reads/writes and same-record function families publish. Entry var/function
instantiation is derived and independently checked, but publication retains
`ScriptScopeError::kHostGlobalAccessor` for the additional host-accessor boundary
below. This is an observable-runtime boundary, not sample admission.

## Evidence and scope

The exact SDK commands, compiler SHA-256 and input/source hashes are in
`tests/fixtures/isa/global_scripts/manifest.json`; neither optimization mode uses
`--module`. The compiler is the pinned DevEco es2abc, SHA-256
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
The SDK ark_disasm SHA-256 is
`a82e0c92a6726eb5c515f96c1731984e2a3938a6ce98dc540c7b5e7c08860676`.
Existing [round-9 citations](isa-globals-prototype-throws.md) describe PandaGen
`LoadVar`/`StoreVar`, frontend `compiler/base/hoisting.cpp:24–75`, the C++ handlers
and runtime global record creation/update. Raw lifting now records all global
writes as `ValueGlobalWrite`, with the stored SSA input, name, effects and
exception exit. The existing lifter was implemented, but full semantic lifting
previously failed in its register-effect/operand adapters. No opcode-string
or fixture-specific dispatch was added.

`GlobalReadValues` defaults to the old Unknown LDGLOBALVAR domain. Only V9,
verified actual script ownership selects the language domain; it is not generic
permission to publish a global var read. The printer requires the sealed entry
proof, same-file record/method ownership and a proved var declaration. Old
contracts still fail their original expression/literal obligations.

## Certificate and independent checks

`script_scope.{h,cc}` derives the original global commits, including value input,
name, original OpId, normal/Before effects and exception handler identity.
`VerifyScriptScope` recomputes those facts from the immutable graph and constants.
The first global store is indexed by resolved name; no plan-local CellId or
printed name establishes original-operation correspondence. Original name strings
are used only for the necessary global binding boundary.

A shared three-state normal-CFG dataflow (unreachable / private instantiation /
observed) proves hoisted instantiation precedes observable effects, even when
physical block order differs. Private allocations use the existing resource
failure predicate. This is not a same-block or opcode-sequence rule. Names and
store indexes use sorting; each CFG block is scanned at most twice. Total work
is O((V+E) log V) including indexes, plus string bytes compared. Typed byte/work
failures precede buffer allocation; exact thresholds are tested. At 800 distinct
lexical commits the derivation uses **63,209 admitted bytes / 24,603 work**.

`ScriptScopeProof::Verify` additionally reruns common TreeSemantics, verifies the
actual record's method owners, original global store/AST correspondence, unique
root declaration position and tree revisions. A private frame/cell temporary
cannot leak into the global lexical record: root private symbols refuse with
`kPrivateScope`; block/loop-owned cells retain their native scope. The sealed
proof is forwarded to same-record children, not fabricated module bindings.
New global-var accesses require that proof at printer preflight. The counting
and real sinks use the same script draft and token stream.

`script_tree.cc` is a separate entry route. It reuses common family, class,
evaluation, destination, TreeFlow and recovery verification. It never invokes
module enclosure or fabricates imports/exports. Normal script entry is supported;
phased/exceptional root recovery and unsupported binding scopes retain typed
refusals. Script files end in `.js`, manifest language `javascript-script`.
The strict directive preserves Ark's checked stores and explicit value-call
receiver. The entry's original RETURNUNDEFINED is emitted as an explicit final
`void 0` completion, instead of exposing the last statement's Script value.

## Additional difference: publication kept closed

`ets_runtime/ecmascript/stubs/runtime_stubs-inl.h:1819` passes entry global-var
stores to `JSObject::GlobalSetProperty(..., true)`;
`ecmascript/js_object.cpp:1044–1056` uses SetProperty, invoking an existing setter.
A host installing a configurable accessor named `x` can therefore observe the
entry undefined store (or hoisted function store). Native `var x;` does not
invoke it, and native function declaration instantiation replaces the accessor.
The two approved V9 differences do not explicitly cover this observation.
Until the requested user decision is supplied, var/function declarations refuse
with **ScriptScopeError::kHostGlobalAccessor (10)**. No fixture refusal counts
as support. `test_host_accessor_boundary` demonstrates the standard side using
`vm.Script.runInThisContext`; `vm.createContext` intercepts function installation
itself and is unsuitable for proving this particular standard-global behavior.
There is no Ark VM execution evidence.

## Tests and round trips

The registry contains 18 ABCs (nine sources, default/O0): eight complete lexical,
class and closure/error fixtures, two complete sloppy-write fixtures (approved
behavior difference, deliberately **no successful Node oracle**), and eight
var/function declaration refusals at the host-accessor gate. The historical ten
round-9 probes separately retain refusals except for the two sloppy-write files.
Every positive runs Node syntax and a `vm.Script` differential. The evaluation-
only original appends `void 0` to represent the SDK entry ABI; emitted code must
supply that completion independently. Error cases compare ReferenceError/TypeError
and timing against the same original script; the exact Ark const error source is
checked by retained TRYSTGLOBALBYNAME in disassembly. No synthesized throw exists.

Script-mode support in `grouping_bytecode.py` never inserts `--module`.
`compare_abc_source` disassembles the original ABC and recompiles emitted source
with the same record/source filename and matching default/O0 mode. It retains all
instruction/register/handler/metadata differences. Results: **4 exact / 6 differ /
0 unverified** across the ten completed fixtures. Explanations, not normalizations:

- Default lexical-class fixture: callable instruction bodies identical; physical
  string/literal offsets move by one byte, with identical meanings.
- O0 lexical fixtures: unused original function-self register copies disappear
  (and corresponding register numbering changes); retained `(0, f)` value-call
  syntax can add a dead zero load before the same binding load/call.
- Explicit `void 0` entry completion adds dead `ldai 0; ldundefined` before the
  original RETURNUNDEFINED. It has no user event or exception.
- O0 sloppy fixture also retains the emitted strict directive as an inert string
  load and its physical string offsets. The checked global store stays identical.

All detailed diffs and compiler hashes are under
`build/review/isa-r10-work/roundtrip/`; differences are never counted as bytecode
identity passes. Occurrence-level Ark round-trip semantic-event receipts remain
separate from these strict whole-disassembly comparisons. The oracle importer
now binds source mode to the record and hashes differential runner dependencies;
mutation tests reject wrong modes and stale runners. Ark runtime stays null.


Final acceptance: full build; source-regression 65/65; scale 19/19; ISA 25/25;
focused sweep 192/197 with only the five documented family_source_cli,
tree_structure, try_tree, class_tree_source and try_tree_source failures.
Fresh cohort comparisons against 22b9a31a retain 605/620 and 542/620 with no
text/completion drift. There are no executable scripts in those 40 inputs;
387 no-module records are metadata without func_main_0. The 2,148 sequential
production recovery processes in the ledger peak at 119,947,264 bytes RSS.
The regenerated ledger is 164 Verified / 53 Unverified / 87 Unmeasured /
28 NonExecutable, with no backward verified-profile move. JSON stays in build.
