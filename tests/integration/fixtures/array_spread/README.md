# Source-language array spread

Independently written source; the decompiler receives only `spread.abc`.
Compiler: DevEco Studio bundled es2abc, SHA-256
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

From the repository root, generated on 2026-10-13:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 --record-name spread --output tests/integration/fixtures/array_spread/spread.abc tests/integration/fixtures/array_spread/spread.ts
```

SHA-256:

- `spread.abc`: `21c9575918a1b3911df903ba4d5dbdb485739811204808a5de9270aee022dab3`
- `spread.ts`: `a2d9ba94143f2b412b53ec47e0815f924b28758d2bf7a5d428e89e8fa1be7831`

The CLI test independently decodes all methods: eight `starrayspread` operations,
including three covered by handlers, plus mixed-element `stownbyvalue` writes.
The catch example assigns a result and returns at the join. An initial version
with returns inside both arms generated unreachable trailing bytecode and hit
the then-existing `ValueImportError::kUnreachableFlow` gate. Round 36 adds general
[phased reachability](../../../../docs/design/source-recovery/phased-reachability.md)
separately; this array fixture and its oracle are unchanged.

Node compares the original source and engine output, including event traces:
empty iterables, surrogate pairs, sparse arrays/inherited elements, multiple
spreads mixed with elements, iterator/next/done/value exceptions (inside and
outside a catch), and growth/shrinkage during iteration. Removing a spread or
repeating iteration must change the oracle result.

Round 34 adds late RHS calls, getters, and member calls between spreads and ordinary
elements, both outside and inside a try. Event traces distinguish getter lookup,
receiver identity, call/iteration order, and abrupt completion at each RHS. Moving
the getter ahead of the preceding spread and reading the getter twice are additional
mutations which must change the oracle result.

This is a **source-language oracle**, not Ark runtime differential execution.
The approved LanguageSourceV1 differences are tested intentionally: array
`.values` is ignored in favor of `Symbol.iterator`; next is saved once; inherited,
nonenumerable, and accessor `value` properties are read as standard JS; strings
use standard iteration; length changes follow the standard iterator; destination
prototype setters are bypassed by own element definition. These differences,
including their callbacks and exceptions, are not claimed to be Ark-equivalent.
Cursor overflow is covered by the user-approved resource-failure exclusion;
these tests do not attempt multi-billion-element allocation or RSS measurements.
