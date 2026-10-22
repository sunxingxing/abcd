# ISA round 11: global instantiation and private constructor arguments

V9's global-accessor extension is explicitly approved on 2026-10-22. The eight
previously refused var/function SDK ABCs now publish through the existing sealed
script route. No owner, order, effect, exception or private-scope condition was
removed. LDGLOBALVAR/STGLOBALVAR retain global-var identity rather than becoming
unchecked lexical references. The three approved differences are listed in
[language-source.md](language-source.md); no Ark VM execution is claimed.

## Private NEWOBJAPPLY construction — implemented

[interpreter-inl.cpp:2425–2447](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L2425)
passes the retained constructor and acc array to
[RuntimeNewObjApply:337–362](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/stubs/runtime_stubs-inl.h#L337).
The runtime requires JSArray, snapshots its length, gets each indexed element
in order, then invokes NewObject. It does **not** acquire the array's iterator.
This differs from APPLY's second iteration and does not use or widen the V5
CallSpread premise.

For a fresh, private, dense argument construction, those indexed Gets cannot
invoke user code: every entry is an owned data property, length is the private
array's own length, and the array/cursors cannot escape. Existing
Build/VerifyArrayInitializations prove allocation/copy/phi identity, cursor
coverage, each element/spread commit, full operand order and catch snapshots.
Inner spreads use the existing V1 ArraySpreads premise. CallPlan independently
proves the sole array consumer and a direct fresh function/class constructor
identity. Unknown host values remain `CallPlanError::kConstructorIdentity`:
RuntimeNewObject's Proxy construction path cannot be treated as generic native
`new` without further proof.

The integration adds ValueConstructApply consumers to the existing typed
argument-carrier disposition index, then permits ArrayExpr as a ConstructExpr
carrier. TreeFlow rechecks original operands, private array construction and
exception exits; RecoveryFlow already owns the array carrier's ordered events
and the constructor's exception/commit. Printing omits only the hidden brackets
and allocation. There is no new iterator acquisition or fabricated ExprId.
Array elisions are forbidden in a positional carrier. That syntax check runs
on the single ownership DFS, using its already charged byte-sized role table;
no repeated subtree scans or new per-function buffer are introduced. Discovery
returns before allocating an index when no array-initialization proof exists.

Four SDK ABCs complete at default/O0, proving both NEWOBJAPPLY widths and wide
STOWNBYVALUE cursor commits. The 150 pre-existing property reads in the wide
fixture force IC widths rather than a production width/sample rule. Two unknown-
constructor ABCs retain the exact typed refusal. Node tests trace custom iterator
acquisition/next/value, thrown iterator events, sparse/string sources and mixed
arguments. C++ tests independently mutate constructor/array bindings, cursor
relations, owners and elisions; copies and one-op-per-block variants retain
identity checks. SDK and exact-budget tests cover 200/201/400/800.

At 800 spread+element steps, normal/copy construction admission is 2,755,918
bytes and 1,743,850 CallPlan work; split/copy is 5,167,024 bytes and 3,572,184
work. The tests admit the exact byte boundary and refuse one less; likewise
CallPlan's exact work and one less. These are conditional plan proofs, not a
claim that arbitrary host constructor inputs may publish.

## Remaining misc variants — evidence, not completed support

The new `misc_evidence/manifest.json` records exact SDK commands, hashes,
default/O0 disassembly, real decoding and current typed source refusals.
No refused probe supplies a successful occurrence oracle.

| Family | Pinned meaning / real emitter evidence | Current boundary |
|---|---|---|
| CLOSEITERATOR imm8/imm16 | [C++:1805 / 6715](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L1805); [RuntimeCloseIterator:231–252](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/stubs/runtime_stubs-inl.h#L231) selects Throw from the **pending exception**, else Normal, then IteratorClose. Dynamic PandaGen has an emitter at :1860 but no caller; SDK array patterns use the expanded `compiler/base/iterators.cpp:78+` close protocol, not CLOSEITERATOR. | Both encodings remain observed by the existing constructed fixtures and raw-refused. Complete native array-pattern recovery needs acquisition/next/done/binding and close-completion ownership; the SDK array-pattern probe stops at CatchRegionError. This is not evidence of non-executability. In addition to the missing array-pattern certificate, arbitrary acquisition hits the scope boundary explained below. |
| LDSYMBOL | [C++:1745](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L1745) loads GlobalEnv::SymbolFunction. PandaGen :784 has Constant::JS_SYMBOL, but no dynamic compiler caller was found. Real `return Symbol` probes emit TRYLDGLOBALBYNAME at both levels. | A mutable/shadowed global Symbol is observably different from the VM intrinsic. Existing explicit HostSymbolBinding support is preserved; arbitrary native Symbol replacement is not admitted. Not observed is not impossible. |
| STOWNBYVALUEWITHNAMESET both widths | [C++:2929 / 6860](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L2929) defines own property then uses SetFunctionNameNoPrefix on the **original key** in its fast path. [js_function.cpp:948–980](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/js_function.cpp#L948) preserves a non-symbol key as-is. The slow path :1291–1321 converts to a string/symbol. SDK computed anonymous object properties emit it. | A numeric fast-path key can leave numeric `.name`, while native NamedEvaluation uses a string. The two variants retain LiteralError::kNameSettingProof. String/symbol-only recovery additionally needs a computed-key-domain/order certificate; it is not implemented here. Ordinary STOWNBYVALUE_IMM16 is verified for private dense cursor definitions. |
| NEWOBJAPPLY both widths | PandaGen :1545; runtime indexed-copy contract above. | Implemented for proven private construction and direct constructors. Arbitrary array/proxy-constructor forms keep their typed refusals; no unconditional spread premise. |
| GETMODULENAMESPACE imm8/WIDE | [C++:6229–6248](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L6229), PandaGen :1946–1950. The index is a **module request index**, not an external binding slot. [module_value_accessor.cpp:343–376](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/module/module_value_accessor.cpp#L343) returns an ESM namespace, but raw exports for native/CJS modules. Both widths are in existing SDK probes. | No generic module-slot reinterpretation is safe. An ESM-only proof must bind request/type, namespace identity, entry lexical initialization and capture forwarding. Unproven native/CJS namespace replacement differs observably from a standard namespace wrapper. Current SemanticError::kUnsupportedInstruction remains. |
| DYNAMICIMPORT | PandaGen :1952, [C++:7570](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L7570), [runtime_stubs-inl.h:1839–1884](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/stubs/runtime_stubs-inl.h#L1839) queues the original specifier. [builtins_promise_job.cpp:206–228](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/builtins/builtins_promise_job.cpp#L206) performs ToString **in the job**. | Native import performs ToString before returning its promise. For an object specifier with a coercion hook, effect/exception timing differs. No premise authorizes this; arbitrary inputs remain unsupported. Primitive-string-only owner/host correspondence is a possible narrower future proof. |
| CALLRUNTIME_TOPROPERTYKEY | [C++:7675](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L7675) runs ToPropertyKey at that event. PandaGen :2282; dynamic [classDefinition.cpp:361,370](../../../references/openharmony/arkcompiler/ets_frontend/es2panda/ir/base/classDefinition.cpp#L361) emits it for computed class fields/methods. The real class-method fixture observes it at both compiler levels. | No runtime difference is assumed. Recovery still needs a whole computed-class-key certificate (definition-time evaluation, order, fresh key cell and member identity); standalone conversion cannot become a later implicit coercion. It remains a typed unsupported operation. |

The standard-side dynamic-import timing and intrinsic/name differences are
runtime deductions backed by these sources, not successful Ark execution tests.

**Array-pattern acquisition also needs a decision for arbitrary inputs.**
`RuntimeGetIterator` (`runtime_stubs-inl.h:1594–1610`) returns a non-callable
`@@iterator` value unchanged. The dynamic frontend's Iterator constructor
(`compiler/base/iterators.cpp:25–42`) then observes its `next` property. Native
array destructuring throws TypeError at GetMethod/Call before that property
read. A non-callable object with a `next` getter therefore distinguishes them.
The standard side is asserted by the Node boundary test; the Ark side follows
from pinned code/disassembly, not VM execution. The V2 IterationAcquisition
approval in language-source.md §5 explicitly authorizes complete synchronous
**for-of** and excludes destructuring from that scope. It cannot silently cover
native `[x] = source`. A broader array-pattern acquisition decision, or a proof
that this exceptional input path is impossible, is needed before generic
publication. The normal close, abrupt close and binding event certificate is
still necessary after that decision; this patch does not claim it exists.
The remaining families are explicitly unfinished; this round does not claim the
entire misc list implemented.

## Round-trip interpretation

Original ABC versus recompiled emitted syntax is checked with the reusable
`grouping_bytecode.compare_abc_source` helper at the matching compiler level,
record and source filename. The final run contains 22 complete ABCs (18 script,
four constructor): 7 strict whole-disassembly matches, 15 reported differences,
zero unverified after the existing lexer-based string-export alias adaptation.
The initial unadapted constructor attempt records four SDK parser failures;
they are retained, not counted as passes. The adaptation only unquotes legal
identifier-valued export aliases. A self-pair is used to run that adaptation
on emitted source, as in the oracle writer; it is not an original/recovered
source-token equivalence claim. Evaluation originals are never engine inputs.

Differences remain in the reports, without register, branch, exception or
metadata normalization:

- Script default differences are physical string/literal offsets. O0 additionally
  retains the already documented inert self-register moves, value-call zero
  loads and explicit undefined entry-completion/directive loads.
- In the constructor fixture, native class printing plus the original retained
  prototype read makes es2abc emit an extra read of the fresh class's immutable
  data `prototype`. It has no user-code/exception effect; IC numbering shifts.
- Existing inferred-name preservation prints an unobserved fresh object with a
  constant string key around the anonymous function constructor. That introduces
  object creation, a string-key own define/name set and a private data read,
  preserving `.name` and `.length`; resource failures are excluded by the
  existing contract. No key coercion or prototype setter can intervene. This is
  existing printer behavior, not a new permission for numeric name-setting.
- Function/class installation order changes anonymous method ordinal names and
  associated metadata positions; bodies are compared with those original diffs
  visible. O0 retains distinct register allocation and unused self copies.

Node checks compare constructor `.name`, `.length`, result arguments and full
iterator/getter/throw traces, including a throwing constructor. Separate
occurrence round-trip receipts reject changed method event sequences; Node
passes never turn the strict disassembly differences into equality passes.
