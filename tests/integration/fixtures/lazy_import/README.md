# ArkTS lazy module reads

These independently written sources exercise a named lazy import, a mutable
export, a call through an imported binding, and a read covered by a user catch.
They are not input to the decompiler. Only the ABC files are engine inputs.

Compiler: DevEco Studio bundled es2abc at
`/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc`.
Compiler SHA-256:
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
Generated on 2026-10-13. This compiler accepts `import lazy` in `.ts`; no `.ets`
extension or extra language option was needed.

From the repository root (replace `$ES2ABC` with the path above):

```sh
"$ES2ABC" --module --merge-abc --opt-level=0 --record-name main --output tests/integration/fixtures/lazy_import/main.abc tests/integration/fixtures/lazy_import/main.ts
"$ES2ABC" --module --merge-abc --opt-level=0 --record-name dep --output tests/integration/fixtures/lazy_import/dep.abc tests/integration/fixtures/lazy_import/dep.ts
```

SHA-256:

| File | Hash |
|---|---|
| main.abc | ec8c88ed256d6fcaa01cd4a1e00ea04f5b3a25259196fa8efc954d10e9ed674a |
| dep.abc | 36c4221e1a208c55925865a9624cb5c6f38e2481235af248fbe9059a296ebac4 |

`lazy_import_cli_test` checks decoded `callruntime.ldlazymodulevar` instructions
and the nonzero `moduleRequestPhaseIdx` flag in the actual ABC. Wide encoding
is exercised by `lazy_module_test` and `module_load_test`. Corrupt phase count
and eager/lazy-label mutations operate on checksummed temporary copies only.

The output retains `import lazy` and is published as `.ets` with manifest
`source_language: "arkts"`. Node cannot execute ArkTS lazy syntax. For the Node
oracle only, both source and recovered output mechanically replace `import lazy`
with `import`. That comparison verifies values, live binding after an exported
mutation, and the eager-adapted module event trace. A frozen-binding mutation is
detected. A separate assertion requires the unadapted output to retain `lazy`.
This is **not** an Ark VM differential test and does **not** verify lazy module
initialization timing, first-access exceptions, or caching under ArkTS execution.
