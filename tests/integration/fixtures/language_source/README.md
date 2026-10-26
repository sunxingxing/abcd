# LanguageSourceV1 fixtures

ES modules compiled once to dynamic ABC for `language_source.mjs`. Provenance:
DevEco Studio's bundled es2abc (`--bc-version` 24.0.0.0), command
`es2abc --module --merge-abc --opt-level=0 --record-name=NAME --source-file=NAME.js --output NAME.abc NAME.js`.

| Fixture | Premise | Expected |
|---|---|---|
| `earlywrite` | A hoisted function writes the binding before its declaration runs (the VM write completes; the source throws) | complete under `--language-source`: source and output both throw `ReferenceError`; identical under the default invocation |
| `hoisted` | Hoisted functions read later `const`/`let` bindings while a host callback runs before the stores | complete under the default and `--language-source`, Node-equal |

`harness.mjs` defines `globalThis.SCALE`, `globalThis.OFFSET` and `globalThis.CALL` and reports the
exported values.

Historical pinned-contract evidence (not a CLI target): handler-covered reads and writes are tested in `family_cells_test` with
successfully imported file-backed phased IR. It asserts the access is Language,
is forwarded across the phased middle function, and causes the normal parent to
refuse with `CaptureCellError::kInput > CaptureError::kScope > ScopeError::kInitialization`.
The previous `tryread` compiler fixture stopped at phased import, so it was
removed from the acceptance test; that unrelated failure did not prove this gate.

```
70459539c015f1a5f4096ddf55c8453ac86028b551704b8e87132725e1f0daa0  hoisted.abc
82d17e4642189de9e0b75b4c6d4bdfca0798eed2bbfbead0d8ec2b71743f35d4  earlywrite.abc
```
