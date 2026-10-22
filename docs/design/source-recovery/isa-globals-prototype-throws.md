# ISA coverage 9: script globals, literal prototypes, throw boundaries

This round adds native prototype setters in private ordinary object literals.
Script globals and the remaining throw protocols retain their existing refusals.
No language-source predicate or existing contract meaning changes.

## Script/global evidence and decision boundary

The SDK probes are **scripts**, with no `--module` or `--commonjs`. Their exact
commands, source/ABC hashes and compiler/disassembler identities are in
`tests/fixtures/isa/globals/manifest.json`. Both default and O0 are measured;
140 repeated read/write sites force the imm16 checked-store form. Neither
source recovery nor a Node-vs-Ark execution pass is claimed for these files.
The current record route classifies them as metadata-only (`source_status=2`),
not as completed executable records or typed semantic proofs.

Pinned frontend:
[PandaGen::LoadVar / StoreVar, pandagen.cpp:361–439](../../../references/openharmony/arkcompiler/ets_frontend/es2panda/compiler/core/pandagen.cpp#L361)
distinguishes global `var` (LDGLOBALVAR/STGLOBALVAR), unresolved names
(TRYLDGLOBALBYNAME/TRYSTGLOBALBYNAME), and global lexical declarations
(STTOGLOBALRECORD for let/class; STCONSTTOGLOBALRECORD for const).
[hoisting.cpp:24–75](../../../references/openharmony/arkcompiler/ets_frontend/es2panda/compiler/base/hoisting.cpp#L24)
initializes global vars/functions. The checked store emitter at
[pandagen.cpp:587–595](../../../references/openharmony/arkcompiler/ets_frontend/es2panda/compiler/core/pandagen.cpp#L587)
does not select a sloppy unresolved-write operation.

The six requested variants are implemented. C++ handlers are
[TRYST imm8:2876 / imm16:6782](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L2876),
[LDGLOBALVAR:3004](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L3004),
[STGLOBALVAR:3209](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L3209),
and [record initializers:6342–6391](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L6342).
Checked stores search the lexical record, reject const updates, then check the
global object's own property; an absent property raises ReferenceError.
Var lookup retains its IC-dependent domain; it is not interchangeable with an
arbitrary lexical/global IdentifierReference.

**A native-script correspondence is not established:**

- [RuntimeStGlobalRecord:806–848](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/stubs/runtime_stubs-inl.h#L806)
  creates the record box when this instruction executes, rejects an existing
  record entry, and stores constness. There is no pre-created Hole entry in
  the emitted script prologue. [RuntimeLdGlobalRecord:1753](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/stubs/runtime_stubs-inl.h#L1753)
  returns Undefined when absent; TRYLD then searches the global object. In the
  fixture `globalThis.lexical=9; typeof lexical; let lexical=1`, standard script
  semantics throw ReferenceError at the read because the lexical binding
  already exists in TDZ. The pinned Ark route instead reaches the existing
  object property before the record initialization. This is a deduction from
  emitted bytecode and pinned runtime, **not an Ark VM execution observation**.
- `missing=4` without a strict directive still compiles to TRYSTGLOBALBYNAME.
  Standard sloppy script execution creates the property; the pinned checked
  store throws on an absent property. Node confirms the standard-script side.
- [RuntimeTryUpdateGlobalRecord:1776–1793](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/stubs/runtime_stubs-inl.h#L1776)
  reports `const variable can not be modified`. This is separate from the
  THROW_CONSTASSIGNMENT diagnostic, which includes the name. Native source
  reconstruction must retain the original error source, not synthesize a
  generic TypeError constructor call.

A proposed future decision must explicitly address global lexical activation /
TDZ and unresolved sloppy writes; the current TDZ predicates are not silently
widened. Even with that decision, script entry/declaration/global-scope ownership
would need a separate certificate rather than passing these scripts through the
module route. No broad GlobalLookup::kVar admission is added here.

## Private object-literal prototype mutation

[interpreter-inl.cpp:5489–5513](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L5489)
reads the prototype from v8, the target from acc, calls SetObjectWithProto and
preserves acc only on normal completion. Both widths have the same meaning.
The asm, assembly, baseline and AOT adapters call the same runtime stub;
[pandagen.cpp:1685](../../../references/openharmony/arkcompiler/ets_frontend/es2panda/compiler/core/pandagen.cpp#L1685)
loads the target before emitting the instruction.
[RuntimeSetObjectWithProto:626–639](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/stubs/runtime_stubs-inl.h#L626)
ignores non-object/non-null prototypes; otherwise it uses SetPrototype with
Throw=true. It rejects JSShared prototypes as an Ark extension; recompilation
retains that same runtime check. No standard-JS sendable execution is claimed.
For a fresh ordinary target and ordinary JS prototypes this is native
PrototypeSetter evaluation, not [[Set]] or DefineOwnProperty.

`ObjectMutationKind::kSetPrototype` is a typed attribute in the existing literal
mutation family. It preserves target/result identity, the separate prototype
input, normal Commit and exception effects. The existing LiteralPlan and its
independent verifier prove allocation identity, private ownership, ordered
operand/field intervals, no escape, and handler correspondence. A nonprivate
target or an unsupported staged interval still refuses; there is no layout or
opcode-sequence admission. CopyProperties keeps its independent V7 predicate.

The tree retains `ObjectPropertyEffect::kSetPrototype`, never inferred from the
string `__proto__`. Ordinary data properties with that name stay computed.
TreeFlow checks the exact original kind, target/result identity and prototype
input. Tree structure checks that only an object literal owns the setter and
that there is at most one per literal; a repeated setter cannot become an early
SyntaxError. RecoveryFlow gives the mutation its own `kObjectPrototype` action
and exception exit on every projection. It cannot be dropped by treating the
aggregate as allocation-only. Both printer sinks emit `__proto__: value` from
this verified effect.

No new whole-graph index is allocated. Mutation lifting is O(1); literal/event
ownership reuses the existing O((V+E) log V) indexes. One boolean in the already
charged syntax traversal frame checks duplicate setters without rescanning.
All existing budgets are unchanged. Exact work/byte boundaries, order/target /
property-kind mutations, copies and block splits run at 200/201/400/800.

SDK default/O0 fixtures cover both widths, primitive/null/object/function
prototypes, nested/private prototypes, new identity, getter/throw order and
800 distinct allocations. A computed own data definition after the setter
currently stays at EvaluationShapeError::kMissingShape; the source's computed
DefineField event is not confused with this prototype event. No accessor,
shared-object or Ark-runtime execution oracle is invented.

## Throw audit: meanings are not interchangeable

All five requested runtime operations are implemented, not NonExecutable:
[interpreter-inl.cpp:4055–4146](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L4055),
[runtime_stubs-inl.h:1706–1751](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/stubs/runtime_stubs-inl.h#L1706).

| Operation | Exact source/runtime role | Current boundary |
|---|---|---|
| THROW_NOTEXISTS | TypeError `Throw method is not defined`; missing iterator `.throw` after IteratorClose in a delegated yield's throw-resume path | Generator/yield* resume/close correspondence, not an undeclared global read; current SDK fixture refuses FunctionEmitError::kKind |
| THROW_DELETESUPERPROPERTY | ReferenceError `Can not delete super property` | Native source round-trip is not established: the SDK emits ordinary delete-this instead; retains raw unsupported refusal |
| THROW_CONSTASSIGNMENT | TypeError `Assignment to const variable ` plus the diagnostic register string | Raw SemanticError::kUnsupportedInstruction remains; a diagnostic name alone cannot establish the actual const binding, RHS order and original throw dispatch |
| THROW_UNDEFINEDIFHOLE | Conditional Hole test; diagnostic name register read only on failure; ReferenceError plus original name | Raw lifting exists. The two-input guard still lacks source binding/diagnostic-name correspondence (EvaluationShapeError::kMissingShape); WithName TDZ recovery is not expanded by name guessing |
| THROW_IFSUPERNOTCORRECTCALL | Require initialized/uninitialized this according to mode; ReferenceError on an invalid super protocol | Existing derived-constructor proof handles ordinary narrow checks. The valid O0 derived fixture completes and supplies Node evidence; default retains the pre-existing CatchRegionError::kHandlerShape; twice/missing-super fixture retains DerivedProjectionError::kProtocol |

The first row's frontend is
[functionBuilder.cpp:210–236](../../../references/openharmony/arkcompiler/ets_frontend/es2panda/compiler/function/functionBuilder.cpp#L210),
not a name lookup.
[PandaGen::ThrowConstAssignment:2115–2122](../../../references/openharmony/arkcompiler/ets_frontend/es2panda/compiler/core/pandagen.cpp#L2115)
materializes a diagnostic string, not a binding reference.
[unaryExpression.cpp:36–69](../../../references/openharmony/arkcompiler/ets_frontend/es2panda/ir/expressions/unaryExpression.cpp#L36)
compiles any member delete through CompileObject/CompileKey and DeleteObjProperty.
The real SDK probe `delete super.x` emits `lda this; ... delobjprop`, not the
throw opcode. Node observes ReferenceError for the original JS and successful
own-property deletion for the SDK-ABC recovery. The test asserts this difference
and emits **no** successful occurrence oracle for that pair. A request to print
native delete-super for THROW_DELETESUPERPROPERTY therefore needs a separate
execution/round-trip decision; this round does not choose one.

The remaining const/guard/generator boundaries above are implementation gaps,
not claims that a language premise would solve their binding/event proofs.
The requested throw family is **not complete** in this patch.

## Recorded acceptance

The final build succeeds. Source-regression is **65/65**; every scale test is
**17/17**, ISA is **23/23**, and the touched sweep is **191/203**. The 12 sweep
failures are the documented family/tree/try and unguarded receiver/literal
families. An additional scan of all unit/integration expectations mentioning
`__proto__` selected 40 tests by executable/fixture identity: 37 pass, with only
the known tree_structure, phased_literal and phased_arm_selection_source
failures. No old spelling expectation was relaxed.

Both fresh 40-input comparisons against **dac3bfb5** retain **605/620** under
language-source and **542/620** under previous flags, with zero regressions,
new completions or changes to complete source text. The contract stays V8.

The ledger at `build/review/isa-r9` measures 1,056 ABCs / 1,565,773 instructions,
with zero probe failures. Counts are **159 Verified / 54 Unverified /
91 Unmeasured / 28 NonExecutable**, from **156 / 55 / 93 / 28**. Both prototype
widths and CREATEOBJECTWITHBUFFER_IMM16 become Verified in profile 6. No prior
Verified profile regresses. The two new producers supply 76 executed Node
cases (70 prototype cases, 6 valid-super cases); four additional allocation
scale records pass Node checks/differentials. The deliberately mismatching
SDK delete-super observation is not an oracle. Ark runtime is null.

The reusable strict `grouping_bytecode.py` comparison uses four prototype
records and the O0 valid-super record. Both default and O0 give **2 exact /
3 differ / 0 unverified**. All five emitted files pass Node syntax. The two
wide-prototype records are exact whole-record matches. In the other two
prototype records, every callable body is exact: the entry installs independent
function objects in sorted export order rather than source order. No user
callback runs between these private creations/stores; their literal/string
physical positions also shift. These remain differences in the strict report.

The fifth comparison is the existing derived-constructor lowering: the private
base class is inlined once into the heritage, changing encoded method nesting
and physical registers/metadata. The original constructor's return-of-super
undefined branch becomes an unused `super(arg) === undefined` check followed
by the implicit this return. Super Commit is an object, so the comparison is
inert and both constructors return that same receiver. Ordinary Node cases
match. No disassembly normalization was used, and this is not called an exact
bytecode pass. The separate occurrence event oracle retains its mismatches as
null. Full source-recovery pairs are not grouping-only edits: the grouping
Acorn checker reports all five as token differences, also retained explicitly.

Logs, exact compiler/disassembler commands and diffs are under
`build/review/isa-r9-work/`; generated JSON stays under build. No commit, push
or stash was performed.
