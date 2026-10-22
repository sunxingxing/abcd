# Intrinsic RegExp literal recovery

The two CREATEREGEXPWITHLITERAL encodings read a pool string and runtime flag
bits, allocate a fresh RegExp, and commit the accumulator only on success.
Pinned OH7 evidence (all paths relative to `references/openharmony/arkcompiler`):

- `ets_runtime/ecmascript/interpreter/interpreter-inl.cpp:2544–2572` decodes both
  widths, calls the slow stub and checks its abrupt completion before commit.
- `ets_runtime/ecmascript/interpreter/slow_runtime_stub.cpp:683–689` and
  `ets_runtime/ecmascript/stubs/runtime_stubs-inl.h:2534–2538` delegate to
  RegExpCreateWithRawFlags. `builtins/builtins_regexp.cpp:2523–2536` calls
  RegExpAlloc on every evaluation. Cached compiled regexp code is not a cached
  RegExp object; lastIndex remains private to each evaluation.
- `ets_runtime/ecmascript/regexp/regexp_parser.h:40–47` defines g=1, i=2, m=4,
  s=8, u=16, y=32, d=64. `builtins/builtins_regexp.cpp:2376–2416` emits flags in
  dgimsuy order. There is no v flag. The frontend lexer enum has a different bit
  layout and is deliberately not used for raw bytecode.

## Certificate and limits

The raw lifter reuses StringLoad/ValueString with a typed RegExp kind and flags.
It has allocation/throw effects and a Defined result, never String or Pure.
The source tree has RegExpExpr. The operand verifier checks the original pool
identity and flags, and the structure/printer independently validate the token.
No mutable global RegExp lookup is synthesized for the intrinsic operation.

BuildRegExpLiteral uses an explicit prepaid group stack and one prepaid output
buffer. It recognizes a conservative, linear-time subset of Pattern grammar,
including ordinary atoms, classes/ranges, assertions, groups, alternatives,
quantifiers, character/control/hex/unicode escapes. Unsupported backreferences,
named groups, property escapes and Annex B ambiguities retain a typed Unproven
refusal. Invalid admitted syntax has a typed Syntax refusal. Unknown flag bits
refuse. This never turns a possible runtime syntax error into an early source
syntax error. Decimal quantifiers beyond the runtime's signed limit refuse.

Empty patterns print `/(?:)/`. All unescaped slashes are escaped, even inside
classes, to match the pinned observable source getter
(`builtins_regexp.cpp:2539–2563`). Actual LF/CR/U+2028/U+2029 and lone surrogate
pool characters keep a typed SourceRepresentation refusal: native escaping
would change the pinned getter's source spelling. Escaped `\\n`, `\\r`, and
`\\u2028`/`\\u2029` text is supported. No extra source-language premise is used.
Build and independent re-derivation are O(pattern length); exact byte/work
boundaries exercise both width and depth at 200/201/400/800.

## Evidence layers

The pinned frontend `ets_frontend/es2panda/ir/expressions/literals/regExpLiteral.cpp:30–58`
lowers source literals to a global RegExp constructor. SDK default and O0 probes,
including 140 property sites exceeding the IC-byte range, confirm that neither
intrinsic encoding is emitted. Four constructed factory modules cover both encodings
in profiles 12.0.6 and 24.0.0; the imm16 IC operand is 300. Both engine and SDK
decoders accept them. Two constructed loop modules also check that lastIndex is fresh on every
iteration. Two additional SDK modules cover ordinary source lowering and a loop
observing fresh lastIndex.

All eight records must complete and pass Node differentials against separate
runtime-derived JS oracles. They check canonical source/flags, matching/indices,
lastIndex, per-evaluation identity and independent state. The shared occurrence
writer records only executed Node assertions; Ark runtime remains null.

Strict grouping_bytecode checks compile oracle and recovery under default and
O0. Using the same function-expression declaration surface, the four constructed factory
fixture pairs compare exactly. The SDK pairs differ in entry declaration order
(hoisted declarations become ordered source bindings) and the loop's proven
numeric induction removes a redundant ToNumeric before increment; these are
reported, not normalized away. Independently, original intrinsic -> SDK
recompilation has a semantic-event mismatch: the SDK adds a global RegExp lookup
and construction. In particular, replacing global RegExp can distinguish this
recompilation from the intrinsic. No immutable-global premise or Ark round-trip
pass is inferred. The Node oracle and null round-trip evidence are separate.

## Coverage-6 verification checkpoint

The eight ABC records complete and pass syntax checks and **1,342 Node cases**.
The occurrence transaction has 1,674 Node rows, 775 matched recompiled events
and 899 null round-trip events; no Ark runtime result is claimed.

For each optimization mode, strict grouping_bytecode reports **4 exact / 4
different / 0 unverified** records. The two additional constructed loop pairs
swap compiler-private counter/sum registers, introduce explicit private bindings
and snapshots at O0, and omit the redundant ToNumeric on the number-only counter.
The recovered for-update clause contains the same ordered per-iteration RegExp
allocation, lastIndex read and write. It executes once after each successful
condition and has no abrupt body exit in this fixture. These differences are
retained in the diffs, not accepted through a register/branch normalization.
Across all eight pairs there are 102 named function bodies; the four factory
records and all non-loop SDK functions compare exactly. The four loop functions
account for the body differences. Module-entry hoisting/order differences are
reported separately from function-body matching.

The final ledger at `build/review/isa-r6` scans 920 ABCs with no probe failures:
**143 SupportedVerified / 58 SupportedUnverified / 103 Unmeasured / 28
NonExecutable**. Both RegExp variants move from Unmeasured, with independent
profile-4/profile-6 evidence. JEQZ imm16, NewObjRange imm16 and TryLdGlobalByName
imm16 additionally move from SupportedUnverified in profile 6. No variant or
verified profile moves backwards.

Both cohort comparisons against 087cf100 remain **605/620 source** and
**542/620 previous**, without new completions, regressions or text changes to
previously complete records. RegExp unit, malformed operand, receipt, token,
pattern/flag mutation and exact-budget checks pass. At 800, width uses 1,609
work / 4,026 admitted bytes; depth uses 4,009 work / 8,026 admitted bytes.
The full build, source-regression, scale and ISA results are indexed in status.md.

Tagged templates remain an implementation gap; see
[the construction checkpoint](isa-template-construction.md). No tagged-template
opcode is promoted by this RegExp work.
