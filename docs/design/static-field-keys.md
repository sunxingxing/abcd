# Static Field Key Syntax

The class writer now selects literal ASCII IdentifierName keys for already
proven static fields. Non-identifier, empty and non-ASCII names retain the
existing escaped computed-string form. A typed MemberKeySyntax boundary selects
the spelling; no printed-source inspection or opcode-name dispatch is involved.

The `constructor` and `prototype` names keep computed spelling under
[ECMA-262 2024 class early errors](https://tc39.es/ecma262/2024/multipage/ecmascript-language-functions-and-classes.html#sec-class-definitions-static-semantics-early-errors).
The independent class ownership/value/descriptor proof still decides whether a
field is admitted; in particular this syntax choice does not bypass the existing
prototype rejection. Initializers remain in their original order and class
context. Field definition PC spans surround the full new spelling as before.

Classification and output are linear in key length with fixed scratch. Both
production targets built successfully, and read-only review found no concrete
issue. ABC-only export `build/pasteboard-static-keys-01` emits FULL_PERCENT,
TEXTAREA_HEIGHT and BREAK_POINT_SM as literal static fields. Eight incomplete
app records, two host-constructor refusals at PC14 and nine metadata records
remain. Method maps retain 195 entities / 9,745 spans.

Tests, generated JS execution and RSS remain paused. Deferred regressions cover
constructor/prototype, contextual names, __proto__, non-ASCII and escaped keys,
initializer order, descriptor admission and generated positions. This is source
syntax recovery, not complete source or runtime-equivalence verification.
