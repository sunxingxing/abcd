# Tagged-template construction (ISA coverage 7)

GETTEMPLATEOBJECT now has a typed VM intrinsic and a private-construction
certificate. Native publication requires the existing V7 `TaggedTemplates()`
predicate. Earlier contracts still refuse. This round introduces no new premise.

## Pinned meaning and order

- [interpreter-inl.cpp:5300–5330](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L5300)
  implements both modern widths and the historical register form.
- [template_string.cpp:22–70](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/template_string.cpp#L22)
  reads carrier[0] (raw), performs the identity-keyed cache lookup, reads
  carrier[1] (cooked), copies their indexed values and freezes the resulting
  template/raw objects. V7 accepts the indexed descriptor and fresh-array cache
  differences. Arbitrary carrier objects are not admitted.
- [literals.cpp:26–78](../../../references/openharmony/arkcompiler/ets_frontend/es2panda/compiler/base/literals.cpp#L26)
  allocates raw/cooked arrays, interleaves their element definitions, then builds
  the carrier. Invalid escapes put Undefined in the cooked array.
- [taggedTemplateExpression.cpp:45–80](../../../references/openharmony/arkcompiler/ets_frontend/es2panda/ir/expressions/taggedTemplateExpression.cpp#L45)
  saves the tag and member receiver first, builds the template object, evaluates
  substitutions left to right and finally invokes the saved tag.
- The SDK uses CALLRUNTIME_DEFINEFIELDBYVALUE for these array definitions.
  [interpreter-inl.cpp:7639](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L7639)
  and [runtime_stubs-inl.h:3450–3465](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/stubs/runtime_stubs-inl.h#L3450)
  establish ToPropertyKey followed by CreateDataPropertyOrThrow, with writable,
  enumerable and configurable all true. Lifting reuses ComputedOwnWrite with
  `OwnWriteKind::kDefineField`; it never substitutes [[Set]].

## Certificate and consumption

`template_construction.{h,cc}` derives fresh allocation identities through the
verified evaluation aliases. A shared definition/use index proves dense,
ordered raw/cooked entries and the carrier's exact raw/cooked references. Keys
must be proven nonnegative integer constants, raw entries string constants,
and cooked entries strings or Undefined. The arrays have no observation,
escape, phi or other consumer outside this construction. Original effect-order
must-reach edges certify the whole interleaving, rather than a fixed opcode
sequence, same-block rule or PC layout. Pure register transport remains an alias.
A unique GETTEMPLATEOBJECT result is the first argument of one ordinary tag call.

The certificate retains each original allocation/definition Commit, the
intrinsic, its call, and their exception dispatch identities. Fresh arrays with
primitive own keys have no JS callback or inherited setter invocation; only the
already-approved resource-failure exclusion permits omitting their hidden
construction syntax. The intrinsic and invocation remain separate ordered,
throwing source actions. Ordinary CallPlan reference/order and operand checks
prove the saved tag, exact receiver, substitutions and successful result.

Evaluation verifies a fresh derivation before claiming private members. Literal
planning receives their ownership projection; the general allocation exclusion
is unchanged. These internal arrays never become source symbols or temporaries.
TreeFlow/operand checks bind the intrinsic to TemplateObjectExpr with exact
raw/cooked pool identities. OriginalFlow owns an immutable, charged input and
text snapshot. Every recovery proposal re-derives construction and checks its
original uses, blocks, operations, edges, dispatches, unique call position and
quasis against the current tree. Substitution/callee/receiver correspondence
continues through the ordinary call inputs and flow verifier.

`template_literal` independently computes TV from TRV, including Unicode/hex
escapes, identity escapes, line continuations and invalid tagged escapes. Invalid
escapes require cooked Undefined. No printer guesses cooked strings. Raw text is
written verbatim: its existing backslash escapes preserve backticks, `${` and
backslashes. Blind escaping would change String.raw. Raw delimiters without an
equivalent TemplateCharacters token, CR spellings and literal lone surrogates
stay typed refusals. Both token sinks use the same actions; the template object's
span covers its actual backtick/substitution text.

## Cost and tests

One method scan and shared use/effect-order indexes precede per-array sorting:
O((V+E) log V + total string units), O(V+E+string units) storage. No per-site
whole-graph rescan is used. Every vector, shared owner, retained snapshot, text
buffer and explicit printer stack is charged before allocation. Methods without
the intrinsic allocate no construction index.

`template_construction_test` checks both raw widths, malformed operands,
private identity/definition mutations, wrong index, escape, [[Set]] substitution,
missing exception/changed handler, missing event, source call ownership and
cooked/raw mismatch. Normal and Before/Commit split graphs pass. Exact byte/work
boundaries cover 200/201/400/800 quasis and string units. At 800 quasis derivation
uses **438,217 bytes / 113,093 work**; 800 substitutions (801 quasis)
use **438,763 bytes / 113,234 work**. Reducing either exact grant by one refuses.

Eight registered ABCs cover SDK default/O0 (plain, member, String.raw, invalid
escapes, nested templates, effectful/throwing substitutions and throwing tags),
forced imm16 SDK sites, and both widths in profiles 12.0.6 and 24.0.0. Node checks
raw/cooked values, freezing, receivers, saved-callee replacement, exception and
substitution order. It excludes only the two approved V7 observations; Ark VM
execution is unmeasured. The occurrence evidence writer does not promote a
round-trip mismatch to a pass.

The SDK emits WIDE_CALLRANGE for a single tag with 200+ substitutions. That
separate call encoding still refuses: the pinned C++ interpreter uses
READ_INST_8_3 at interpreter-inl.cpp:1483–1489, while the asm stub uses
ReadInst8_2 at interpreter_stub.cpp:5092–5106. This round does not choose a new
execution-path premise. The certificate itself is tested through 800
substitutions, and end-to-end SDK scaling uses 200/201/400/800 tag sites and
raw-string lengths, keeping this unrelated call refusal explicit.


## Final acceptance

The final ledger is 149 Verified / 55 Unverified / 100 Unmeasured /
28 NonExecutable, with no backward profile moves. Eight complete fixture records
pass 172 Node cases. Their 1,863 occurrence attestations retain separate oracle
columns: 1,074 exact ordered-event matches (the two SDK O0 records), 789 null
matches, and no Ark runtime evidence. The SDK default records have 681 null
matches because the shared writer recompiles at O0 and conservatively requires
an identical whole event sequence, including transport copies. The four
constructed records have 108 null matches because there is no unique encoded
method identity in the compiler output. These are explicit mismatches, never
partial round-trip passes.

The separate grouping_bytecode.py comparison compiles source oracles and emitted
source at default and O0 independently. The oracle's module function/export
carrier is adapted to the same let-bound function form used by existing source
recovery; function bodies are unchanged. Each mode gives six exact whole-record
matches and two reported differences. All nine callable methods in each of the
two differing SDK records are exact; their module entry alone installs the
independent functions in a different order. No instruction/register/handler
normalization or permissive comparison mode is used. Both sides use the same
string-export adaptation. These source-vs-source results are not represented as
exact original-ABC equality for the constructed encodings.

Final build and checks: source regression 65/65, scale 14/14, ISA 18/18,
targeted 5/5; the touched sweep passes 219/231 with only its 12 documented
pre-existing failures. Fresh cohorts remain 605/620 and 542/620, with no status
regressions or changed previously complete text. Artifacts are under
`build/review/isa-r7` and `build/review/isa-r7-work`.
