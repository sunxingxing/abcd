# Explicit language arithmetic

**Historical / superseded CLI (2026-10-26).** The selector described below has
been removed. Source recovery defaults to `language_source_v10`;
`--language-source` is its optional alias. The remainder records the original
approved premise and evidence, not current CLI instructions. Pinned runtime
analysis facts remain valid. See [the current contract](language-source.md).

`SourceContract::kLanguageArithmeticV1` is an opt-in numeric equivalence
premise built on the host, scope, resource and callable-text rules of V3.
The default remains `kLanguageAndHostV3`. It is not a certificate of measured
agreement with a particular Ark binary.

The mode restores binary arithmetic according to ECMAScript numeric types
(6.1.6 and the binary expression evaluation rules): Number/BigInt arithmetic,
ADD string concatenation, shifts and bitwise operators. Normal completion
produces a defined language value. Conversions, operand evaluation, abrupt
completion and observable state changes remain part of the unchanged typed
instruction and common effect/exception analyses.

This premise does not promise a particular engine's undefined C++ behavior,
NaN payload bits or internal tag layout. Observations depending on these
implementation details are outside this mode's equivalence declaration.
Pinned-runtime mode retains its existing representation restrictions. Neither
mode reclassifies impure literal encodings, Hole, environment values or
uninitialized registers as language values. See [numeric facts](numeric-facts.md).

## Common implementation

The source contract maps to typed `ArithmeticSemantics`. FunctionInput and
PhasedBody pass that premise into the same scalar value-rule adapter; normal
and phased SSA retain the common sparse fixed-height solver. Linear lexical
analysis uses the same rule and conditional-result refinement. A result is
never seeded before its actual operands are proved. Known finite/non-NaN,
String and integer refinements remain available for expression composition.
The mode and contract identity are retained in proofs, receipts and manifests.

Binary operators keep their original ValueBinary and native BinaryExpr
lowering. There is no alternative writer, sample/name dispatch, local opcode
sequence, register/PC pattern, same-block check or producer-list admission.
The common printer owns precedence and parentheses. Captures, module/local
initialization, receiver identity, execution count and effect/exception order
continue to use their existing owners.

Unary conversion/negation/update profiles and constructor protocols retain
existing rules. In particular, legacy ToNumber behavior is not replaced by
modern BigInt-aware unary syntax just because binary arithmetic is enabled.
Unknown parent constructors are not admitted by this numeric premise.
The separately approved [LanguageNumericV1](language-numeric.md) adds unary
numeric recovery through a distinct selector; it does not reinterpret V1.

## Tools and evidence

Use `--language-arithmetic` together with `--value-source` in abc_structure
or abc_semantic. Options are validated before file access; duplicate flags,
legacy/detail modes and incompatible selectors are rejected in either order.
Receipts and manifests serialize `language_arithmetic_v1`. Existing V1/V2/V3
labels and the default CLI behavior retain their previous meanings.

`language_arithmetic_test` decodes all three supported dynamic profiles and
builds complete ordinary and try/catch functions through the shared tree.
Its independent Node oracle covers native binary composition and parentheses,
Number boundaries, NaN, signed zero, strings, BigInt/Symbol, conversion getters,
receiver identity, effect count/order and original thrown tokens. It also
checks private input rejection, contract/source identity and 200/201/402
scaling. Typed refusal is not counted as a positive arithmetic scenario.

`language_arithmetic_paired_test` generates two actual ABC records before
opening paired TypeScript. It compares registered key callbacks from Content
and PictureDetail and Content's resource-array creation in a declared host
fixture. It does not certify complete UI execution or constructor acceptance.
The fixed-five census and remaining phase-one gate are recorded in status.md.

Provenance: pinned OpenHarmony-v7.0-Release runtime identities in
`docs/reference/openharmony-v7.0-release.lock.json`; ECMAScript numeric types
<https://tc39.es/ecma262/multipage/ecmascript-data-types-and-values.html#sec-numeric-types>.
The transfer extension adds O(1) work per existing scalar node and retains
O(V+U) sparse-solver storage. No new dependency or per-variable CFG walk.
