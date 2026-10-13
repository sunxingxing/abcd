# Source-language array spread

Independently written source; the decompiler receives only `spread.abc`.
Compiler: DevEco Studio bundled es2abc, SHA-256
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

From the repository root, generated on 2026-10-13:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 --record-name spread --output tests/integration/fixtures/array_spread/spread.abc tests/integration/fixtures/array_spread/spread.ts
```

SHA-256:

- `spread.abc`: `80c7d5c969bad6a2434920842cf5d348a95123524688b99a7aee0b83832e36e9`
- `spread.ts`: `addc57bd252320e90163d2f6b06f4f34fc5be24c8ddbf339d2716edc5f6ee8bd`

The CLI test independently decodes all methods: four `starrayspread` operations,
including one covered by a handler, plus mixed-element `stownbyvalue` writes.
The catch example assigns a result and returns at the join. An initial version
with returns inside both arms generated unreachable trailing bytecode and hit
the existing `ValueImportError::kUnreachableFlow` gate. That gate was not changed.

Node compares the original source and engine output, including event traces:
empty iterables, surrogate pairs, sparse arrays/inherited elements, multiple
spreads mixed with elements, iterator/next/done/value exceptions (inside and
outside a catch), and growth/shrinkage during iteration. Removing a spread or
repeating iteration must change the oracle result.

This is a **source-language oracle**, not Ark runtime differential execution.
The approved LanguageSourceV1 differences are tested intentionally: array
`.values` is ignored in favor of `Symbol.iterator`; next is saved once; inherited,
nonenumerable, and accessor `value` properties are read as standard JS; strings
use standard iteration; length changes follow the standard iterator; destination
prototype setters are bypassed by own element definition. These differences,
including their callbacks and exceptions, are not claimed to be Ark-equivalent.
Cursor overflow is covered by the user-approved resource-failure exclusion;
these tests do not attempt multi-billion-element allocation or RSS measurements.
