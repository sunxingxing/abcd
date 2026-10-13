# LanguageSourceV1 fixtures

ES modules compiled once to dynamic ABC for `language_source.mjs`. Provenance:
DevEco Studio's bundled es2abc (`--bc-version` 24.0.0.0), command
`es2abc --module --merge-abc --opt-level=0 --record-name=NAME --source-file=NAME.js --output NAME.abc NAME.js`.

| Fixture | Premise | Expected |
|---|---|---|
| `hoisted` | Hoisted functions read a later `const`/`let` (textually after the declaration, so compiled without a hole check); one also writes it | complete under `--language-source`, Node-equal; refused otherwise with `CaptureCellError::kInput → CaptureError::kScope → ScopeError::kInitialization` |

`harness.mjs` defines `globalThis.SCALE` and `globalThis.OFFSET` and reports the
exported values.

```
2a6f1c60bc8e413a033cc6187ce6696083145106067f7e665aecd3ab665fc603  hoisted.abc
```
