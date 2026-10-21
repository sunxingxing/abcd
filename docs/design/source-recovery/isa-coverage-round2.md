# ISA coverage 2 — measured progress and remaining gates

Baseline: ef018a26. This is a partial implementation of the requested programme,
not completion of every requested family. The generated ledger remains
occurrence/profile-bound; no fixture label or successful method invents an
oracle. SDK commands, hashes, exact observed encodings and recovery refusals
are in `tests/fixtures/isa/recovery/manifest.json`. Production receives ABC only.
The SDK compiler SHA-256 is
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`;
ark_disasm is
`a82e0c92a6726eb5c515f96c1731984e2a3938a6ce98dc540c7b5e7c08860676`.
Pinned source identities remain those in
[the release lock](../../reference/openharmony-v7.0-release.lock.json).

## Implemented semantics

* **LDSUPERBYVALUE, both widths:** the encoded register supplies the receiver,
  accumulator supplies the property key, and the executing function supplies
  the home object. See [interpreter-inl.cpp:5669](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp:5669)
  and [RuntimeLdSuperByValue:707](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/stubs/runtime_stubs-inl.h:707).
  The frontend emission is [PandaGen::LdSuperByValue:1975](../../../references/openharmony/arkcompiler/ets_frontend/es2panda/compiler/core/pandagen.cpp:1975).
  Raw lifting reuses PropertyRead with a typed super lookup. Value import,
  source operands, home-object/receiver verification and recovery retain that
  distinction. Ordinary member-update discovery cannot consume a super read.
  Unknown home objects and receiver substitutions still refuse.
* **Computed-super key ordering restriction:** Ark rejects a null super base
  before ToPropertyKey; standard source coerces the key before GetValue rejects
  that base. The independently checked projection therefore requires a proven
  primitive key (current domain representation: number, string, or zero-or-one
  primitive). An unknown object key refuses with the appended typed
  `TreeSemanticError::kSuperKeyDomain`; no existing enum values changed. This
  is an implementation limitation, not a claim that object keys are forbidden
  by JavaScript. A future non-null-home-base proof could discharge the order
  difference without a premise. The fixtures change the home prototype and
  trace getter `this` and failures, rather than assuming a fixed prototype.
* **LDBIGINT:** [interpreter-inl.cpp:3272](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp:3272)
  obtains a regional pool string and invokes LdBigInt. [PandaGen:749](../../../references/openharmony/arkcompiler/ets_frontend/es2panda/compiler/core/pandagen.cpp:749)
  emits the instruction. A typed pool-text kind survives raw/value import.
  BigIntExpr preserves allocation and throwing events; it is not reclassified
  as a freely duplicable string constant. Source validation accepts only
  unsigned decimal/hex/octal/binary BigInt literal digits and appends `n`.
  Invalid/unsupported pool text refuses instead of converting an original
  runtime conversion failure into an early parse error. No BigInt constructor
  lookup is emitted. Number/BigInt mixed arithmetic and conversion throws are
  checked by the Node differential.

No contract version or source-language premise changed. Complete corpus source
must remain byte-identical. New buffers use the existing charged IR/tree arenas;
new lifting is allocation-free. Text validation is linear and prepaid in the
structure budget. Source traversal stays iterative. Tests cover 200/201/400/800,
exact byte/work boundaries, block splitting, receiver/home/key substitutions,
unknown key domains, malformed widths/kinds and invalid BigInt text.

## Completing SDK evidence

The fixture suite compiles each source at default optimisation and O0. It records
both successes and exact typed refusals. Only complete exports run the Node
oracle/writer; no refusal is counted as support. Hundreds of IC sites force
imm16 caches; 260 module bindings/imports force wide indexes. The synthetic
modules are initialized dependencies and support live export updates.
`import lazy` is removed by a lexer **only for Node evaluation** of those already
initialized dependencies. Lazy initialization timing is explicitly excluded from
that oracle; the compiler round-trip retains the modifier.

* `property-wide`: Proxy read/write/key conversion and throwing cases.
* `super-named`, `super-computed`: home mutation, receiver identity, throwing
  getter at several evaluation positions; super-to-this mutation is observed.
* `module-local`, `module-external`, `module-lazy`, `module-sendable`: wide
  bindings and live imports; a changed synthetic dependency changes the result.
* `method-accessor`: a class accessor created after many IC sites, forcing
  DEFINEMETHOD_IMM16; getter-prefix failures and accessor descriptors.
* `iterator-return`: GETITERATOR_IMM16 after many property sites; normal,
  exhaustion, early return, next failure and return-method failure traces.
* `bigint`: literal bases, large values, unary minus, arithmetic, mixed types,
  conversion exceptions; a changed literal is detected.

Default-optimized module bodies currently lack the guards required by the
internal-value proof (`SemanticProofError::kInternalValue` /
`InternalValueReason` value 6); the optimized super class creators hit
`EvaluationShapeError` value 0, and the optimized sendable constructor hits
`SendableError` value 1. These measured refusals are not declared impossible to
compile and are not oracle successes. Their O0 counterparts complete. Wider
support requires those existing generic proofs, not a fixture exception.

## Items requiring a user decision or a stronger local proof

These are **candidates**, not approved premises, executable Ark tests, or new
ledger NonExecutable classifications. No code changes admit them.

### Tagged templates / GETTEMPLATEOBJECT

[TemplateString::GetTemplateObject:22](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/template_string.cpp:22)
keys its map by the supplied raw-array object, not by bytecode PC.
[TemplateMap::IsMatch:26](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/template_map.h:26)
compares tagged object identities directly. It creates
raw/cooked indexed descriptors with `(writable=true, enumerable=false,
configurable=false)`, then freezes the arrays. The argument order is confirmed
by [PropertyDescriptor:74](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/js_object.h:74).
Standard tagged-template indexes are enumerable. Further,
[Literals::GetTemplateObject:26](../../../references/openharmony/arkcompiler/ets_frontend/es2panda/compiler/base/literals.cpp:26)
allocates fresh raw/cooked/argument arrays in the expression evaluation before
GetTemplateObject. The actual SDK O0 `current/expressions-o0.abc` disassembly
also has createemptyarray for raw/cooked/argument arrays, definefieldbyvalue
for the strings, then gettemplateobject (retained as
`build/review/isa-r2-work/tagged-sdk.pa`). A single printed occurrence alone therefore does **not**
prove the pinned runtime has standard per-site identity. The SDK and pinned
runtime are separate evidence layers; no VM is installed to conflate them.

Decision question: may native tagged templates accept different indexed
property descriptors and, for the pinned fresh-array lowering, repeated-call
identity? Otherwise require an independently proved stable raw-array identity
and an unobservable descriptor domain. Literal raw/cooked text alone is not
that proof.

### COPYDATAPROPERTIES / object spread

[RuntimeCopyDataProperties:453](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/stubs/runtime_stubs-inl.h:453)
gets each own descriptor, changes writable/configurable, and defines it on the
destination. It does not Get each source value. Standard `{...o}` invokes an
own enumerable getter and defines a data property. Ark can copy an accessor
without invoking it; [ValidateAndApplyPropertyDescriptor:1669](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/js_object.cpp:1669)
creates the destination accessor from the supplied getter/setter descriptor. The existing array-spread premise does not cover object
spread. Decision question: accept this difference, or restrict projection to a
proved data-descriptor-only source? Unknown sources remain refused.

### CREATEOBJECTWITHEXCLUDEDKEYS / object rest

[CommonCreateObjectWithExcludedKeys:2565](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/stubs/runtime_stubs-inl.h:2565)
compares excluded keys with abstract equality and copies retained values using
strict SetProperty on the new object. Native object rest uses CreateDataProperty.
An inherited Object.prototype setter can run in Ark; native rest does not invoke
it. A `__proto__` key can also change the Ark destination prototype. Nullish
input returns an empty object in this helper; source destructuring needs its
separate coercibility guard. Decision question: accept the Set/define difference,
or prove a safe key/prototype domain and exact exclusion equality? No unconditional
native projection was implemented.

### LDNEWTARGET — requested documentation only

There is no uniform all-path meaning in the pinned runtime:

* [C++ interpreter:7047](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp:7047): fatal not-implemented handler.
* [InterpreterAssembly::HandleLdnewtarget:7799](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter_assembly.cpp:7799): dispatch without updating acc.
* [asm interpreter stub:6164](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/compiler/interpreter_stub.cpp:6164): DISPATCH(NOP).
* [AOT circuit builder:903](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/compiler/bytecode_circuit_builder.cpp:903): reads CommonArgIdx::NEW_TARGET.

Native `new.target` agrees with the last path, not fatal/no-op execution. Proposed
user decision: explicitly select constructor/new-target source semantics despite
these interpreter differences, with its own contract predicate; alternatively
require an execution profile proving the AOT semantics. It cannot be classified
all-path NonExecutable. No LDNEWTARGET production code or contract was changed.

## Still unfinished, without an approved new premise

The remaining lift-ok target encodings keep their existing constructed
observations under `tests/fixtures/isa/current/<lowercase-variant>.abc` (deprecated
LDLEXENV is in `tests/fixtures/isa/deprecated/`, by historical profile). Those
arbitrary-operand records are not executable JS oracles.

* LDGLOBALVAR needs global-environment/reference proof; LDTHIS and LDFUNCTION
  need a valid ABI/context occurrence rather than a raw load with arbitrary frame.
  LDSYMBOL needs the intrinsic Symbol identity, not an unchecked mutable global.
* Both computed name-setting own-store widths retain kNameSettingProof; numeric
  key `.name` divergence is not covered by string-key publication. Ordinary
  STOWNBYVALUE/STOWNBYNAME imm16 need a completing construction emitted in that
  ISA mode; current SDK probes commonly select DEFINEPROPERTY or buffers instead.
  These are observations, not assertions of impossible frontend emission.
* CREATEITERRESULTOBJ is observed in generator lowering, whose complete source
  recovery is still gated. NEWOBJAPPLY's hidden-array extraction is not covered
  by the approved **APPLY-only** call-spread predicate.
* DEPRECATED_LDLEXENV exposes an internal environment; the constructed fixture
  is not evidence that it is a source value. Register-form
  THROW_UNDEFINEDIFHOLE needs a valid guard-name/context proof. The super-call
  checks emitted by the frontend use indexes 0/1 and select imm8 there.
  A valid constructed class, not an arbitrary instruction, is needed for an oracle.
* WIDE_STOWNBYINDEX needs a valid private sparse/large-index construction;
  WIDE_COPYRESTARGS needs default+rest ABI composition (current syntax check
  requires rest index equal ABC length). Neither has been weakened.
* This-property reads/writes, super stores, RegExp, prototype literal fields and
  intrinsic throw-source recovery are not completed in this partial round.
  RegExp still needs checked flags and literal escaping; prototype fields need
  private-construction order and shared-object handling; throw-source forms need
  their real const/super/destructuring binding proofs. They are not labelled
  NeedsDecision merely because implementation remains.

Measured results and per-variant class changes are appended below after the
acceptance runs. No changed method is normalized into an exact SDK pass.

## Acceptance (2026-10-21)

Ledger: **95 Verified / 72 Unverified / 137 Unmeasured / 28 NonExecutable →
117 / 62 / 125 / 28**. All 332 variants remain observed; 864 inputs, zero
probe failures. No variant or existing profile row moved backward. Promotions
below are profile 24 (internal profile id 6), never transferred to other profiles.
The 47 deprecated variants remain 1 Verified / 10 Unverified / 36 Unmeasured;
none is presumed non-executable.

* `DIV2_IMM8_V8`: SupportedUnverified → SupportedVerified.
* `NEG_IMM8`: SupportedUnverified → SupportedVerified.
* `SUPERCALLTHISRANGE_IMM8_IMM8_V8`: SupportedUnverified → SupportedVerified.
* `LDSUPERBYVALUE_IMM8_V8`: Unmeasured → SupportedVerified.
* `LDSUPERBYNAME_IMM8_ID16`: SupportedUnverified → SupportedVerified.
* `DEFINECLASSWITHBUFFER_IMM16_ID16_ID16_IMM16_V8`: SupportedUnverified → SupportedVerified.
* `STOBJBYVALUE_IMM16_V8_V8`: Unmeasured → SupportedVerified.
* `LDSUPERBYVALUE_IMM16_V8`: Unmeasured → SupportedVerified.
* `LDOBJBYNAME_IMM16_ID16`: SupportedUnverified → SupportedVerified.
* `LDSUPERBYNAME_IMM16_ID16`: Unmeasured → SupportedVerified.
* `GETITERATOR_IMM16`: Unmeasured → SupportedVerified.
* `DEFINEMETHOD_IMM16_ID16_IMM8`: Unmeasured → SupportedVerified.
* `STOWNBYINDEX_IMM16_V8_IMM16`: SupportedUnverified → SupportedVerified.
* `LDBIGINT_ID16`: Unmeasured → SupportedVerified.
* `CALLRUNTIME_DEFINESENDABLECLASS_PREF_IMM16_ID16_ID16_IMM16_V8`: SupportedUnverified → SupportedVerified.
* `CALLRUNTIME_LDSENDABLEEXTERNALMODULEVAR_PREF_IMM8`: SupportedUnverified → SupportedVerified.
* `CALLRUNTIME_WIDELDSENDABLEEXTERNALMODULEVAR_PREF_IMM16`: Unmeasured → SupportedVerified.
* `WIDE_STMODULEVAR_PREF_IMM16`: Unmeasured → SupportedVerified.
* `WIDE_LDLOCALMODULEVAR_PREF_IMM16`: Unmeasured → SupportedVerified.
* `WIDE_LDEXTERNALMODULEVAR_PREF_IMM16`: Unmeasured → SupportedVerified.
* `CALLRUNTIME_LDLAZYMODULEVAR_PREF_IMM8`: SupportedUnverified → SupportedVerified.
* `CALLRUNTIME_WIDELDLAZYMODULEVAR_PREF_IMM16`: Unmeasured → SupportedVerified.

Nine of the 25 lift-ok targets now verify (computed store imm16, named super
imm16, iterator imm16, method imm16, and the five wide module/sendable/lazy
forms). Sixteen still have the gates above. There are 20 SDK fixtures: 13
complete with **107 Node cases**, seven measured default-optimization refusals.
Seven source/dependency mutants are detected in the Node suite. The ordinary
inputs include throwing getters, key conversion, BigInt coercion and iterator
next/return exceptions. Every complete export passes syntax under the stated
Node dialect adaptation; raw lazy-import syntax is not standard Node syntax.
No Ark runtime execution is claimed.

Strict `grouping_bytecode.py` comparison, both default and O0: **7 exact records,
6 different, 0 unverified**. At the method-function-body level (without changing
registers, branches, exceptions or instructions), **38 exact / 5 different**
in each mode. Differences are retained under
`build/review/isa-r2-work/roundtrip-final/compare/`; they are not normalized passes:

* Two method-accessor records have identical methods; only physical pool/string
  offsets shift by one byte. Full assembly comparison still says different.
* The module-local creator gains one private `newlexenv 0x1` from the recovered
  hoisted binding representation. Its exported reads/stores and values match;
  this private allocation is unobservable under the existing resource-failure
  contract. The `read` method is identical.
* Both super fixtures retain the existing `return super()` recovery. SDK adds
  an undefined/derived-return check and associated branches/IC/physical offsets
  compared with implicit constructor return. Existing derived-constructor
  correspondence proves the returned receiver/object. Getter/receiver traces
  are unchanged. The named-super read method is identical.
* The computed-super method replaces a private constant string register by
  repeated `lda.str "p"` before the same super reads. The string load is pure;
  the receiver, getter count and order are unchanged. It is not counted as a
  matched round-trip event where the strict event sequence differs.
* The iterator fixture recovers `if (!value) continue; else return value` and
  an assignment-form noncaptured binding instead of the input's `if (value)
  return value`. Boolean testing has no coercion effects; continue does not
  close, return does. Register/IC/control differences are retained. Node traces
  check exhaustion, return, next throws and return-method throws.

The tool's parenthesis-only token checker rejects all 13 original-vs-recovered
pairs because this is general source recovery (renaming/declarations/control),
not a grouping rewrite. It is not used as a semantic oracle. Identifier-valued
string export aliases are adapted by the existing Acorn tool independently on
each side before compilation; Ark lazy imports are retained for es2abc. No
register, handler or branch normalization is performed. The occurrence writer
separately records every strict event round-trip mismatch as a null oracle.

Both fresh 40-input cohorts against ef018a26: **605/620 source, 542/620 previous**;
zero new completions, regressions or byte changes to complete outputs. Full
build passes. Source regression **65/65**, all scale **11/11**, ISA **13/13**,
new/focused checks **6/6** plus final super/pool checks **2/2**. The touched
sweep is **189/201**; the 12 failures are already-listed family_source_cli,
tree_structure, try_tree, literal_construct, mixed_literal, phased_literal,
class_tree_source, literal_closure/unguarded_literal_closure,
derived_literal_receiver/unguarded_derived_literal_receiver, try_tree_source.
The attempted baseline CTest sweep is not a valid full-suite comparison:
wtbase has only selected targets built and lacks fixture executables. Its
abc_structure is valid and was used for all cohort OLD outputs.

No old printed-spelling expectations were relaxed. The printer only adds a new
BigInt leaf; complete corpus text is identical. Raw evidence snapshots changed
for the new semantic cases and for the explicit `lookup=kObject` field on four
historical indexed-read profile rows. No test now counts a refusal as support.
Exact-budget tests at 800 BigInt loads admit **237432 bytes / 11236 work** and
refuse each immediately smaller boundary. Computed-super tests independently
check tree byte/work boundaries at 200/201/400/800, with split-block variants.
Raw probe peak RSS across this ledger is **42,352,640 bytes**; full recovery RSS
over 80 sequential cohort runs is **41,107,456 bytes** (Darwin wait4 ru_maxrss),
recorded in `build/review/isa-r2-work/rss2/`. `/usr/bin/time -l` could not report
RSS in the sandbox because sysctl kern.clockrate is denied; wait4 supplies the
actual kernel process high-water mark without that dependency.

No commit, push or stash. The unrelated `sample.tar.gz` is untouched.

## Requested 25-variant checkpoint

| Variant | Current class | Constructed raw observation |
|---|---|---|
| `LDGLOBALVAR_IMM16_ID16` | Unmeasured | `tests/fixtures/isa/current/ldglobalvar_imm16_id16.abc` |
| `LDTHIS` | Unmeasured | `tests/fixtures/isa/current/ldthis.abc` |
| `STOBJBYVALUE_IMM16_V8_V8` | SupportedVerified | `tests/fixtures/isa/current/stobjbyvalue_imm16_v8_v8.abc` |
| `LDSUPERBYNAME_IMM16_ID16` | SupportedVerified | `tests/fixtures/isa/current/ldsuperbyname_imm16_id16.abc` |
| `STOWNBYVALUEWITHNAMESET_IMM8_V8_V8` | Unmeasured | `tests/fixtures/isa/current/stownbyvaluewithnameset_imm8_v8_v8.abc` |
| `STOWNBYVALUEWITHNAMESET_IMM16_V8_V8` | Unmeasured | `tests/fixtures/isa/current/stownbyvaluewithnameset_imm16_v8_v8.abc` |
| `GETITERATOR_IMM16` | SupportedVerified | `tests/fixtures/isa/current/getiterator_imm16.abc` |
| `LDSYMBOL` | Unmeasured | `tests/fixtures/isa/current/ldsymbol.abc` |
| `LDFUNCTION` | Unmeasured | `tests/fixtures/isa/current/ldfunction.abc` |
| `CREATEITERRESULTOBJ_V8_V8` | Unmeasured | `tests/fixtures/isa/current/createiterresultobj_v8_v8.abc` |
| `NEWOBJAPPLY_IMM8_V8` | Unmeasured | `tests/fixtures/isa/current/newobjapply_imm8_v8.abc` |
| `NEWOBJAPPLY_IMM16_V8` | Unmeasured | `tests/fixtures/isa/current/newobjapply_imm16_v8.abc` |
| `DEFINEMETHOD_IMM16_ID16_IMM8` | SupportedVerified | `tests/fixtures/isa/current/definemethod_imm16_id16_imm8.abc` |
| `STOWNBYVALUE_IMM16_V8_V8` | Unmeasured | `tests/fixtures/isa/current/stownbyvalue_imm16_v8_v8.abc` |
| `STOWNBYNAME_IMM16_ID16_V8` | Unmeasured | `tests/fixtures/isa/current/stownbyname_imm16_id16_v8.abc` |
| `DEPRECATED_LDLEXENV_PREF_NONE` | Unmeasured | `tests/fixtures/isa/deprecated/deprecated_ldlexenv_pref_none-dynamic1102.abc` |
| `THROW_UNDEFINEDIFHOLE_PREF_V8_V8` | Unmeasured | `tests/fixtures/isa/current/throw_undefinedifhole_pref_v8_v8.abc` |
| `THROW_IFSUPERNOTCORRECTCALL_PREF_IMM16` | Unmeasured | `tests/fixtures/isa/current/throw_ifsupernotcorrectcall_pref_imm16.abc` |
| `CALLRUNTIME_WIDELDSENDABLEEXTERNALMODULEVAR_PREF_IMM16` | SupportedVerified | `tests/fixtures/isa/current/callruntime_wideldsendableexternalmodulevar_pref_imm16.abc` |
| `WIDE_STOWNBYINDEX_PREF_V8_IMM32` | Unmeasured | `tests/fixtures/isa/current/wide_stownbyindex_pref_v8_imm32.abc` |
| `WIDE_COPYRESTARGS_PREF_IMM16` | Unmeasured | `tests/fixtures/isa/current/wide_copyrestargs_pref_imm16.abc` |
| `WIDE_STMODULEVAR_PREF_IMM16` | SupportedVerified | `tests/fixtures/isa/current/wide_stmodulevar_pref_imm16.abc` |
| `WIDE_LDLOCALMODULEVAR_PREF_IMM16` | SupportedVerified | `tests/fixtures/isa/current/wide_ldlocalmodulevar_pref_imm16.abc` |
| `WIDE_LDEXTERNALMODULEVAR_PREF_IMM16` | SupportedVerified | `tests/fixtures/isa/current/wide_ldexternalmodulevar_pref_imm16.abc` |
| `CALLRUNTIME_WIDELDLAZYMODULEVAR_PREF_IMM16` | SupportedVerified | `tests/fixtures/isa/current/callruntime_wideldlazymodulevar_pref_imm16.abc` |
