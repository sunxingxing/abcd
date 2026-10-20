# Enumeration composition fixtures

Independent JS oracles, compiled with the pinned dynamic DevEco frontend. The
engine consumes only the ABC; `enumeration_composition.mjs` executes the original
JS separately and compares values, exception constructors and ordered effects.

| Fixture | Export / method | Obligation |
|---|---|---|
| main | keys / 411 | A for-in acquisition and a subsequent for-of share an already-proven transparent exception route. |
| decision | compare / 383 | A retained Boolean selection is a conditional statement-prefix event, not a second fused evaluation. |
| local_transfer | scan / 405 | Break/continue in an ordinary inner loop stay inside the active for-of. |
| set_equal | sameMembers / 504 | Captured arrays, ordinary loops and a for-of; Number-only conversion identity and close correspondence. |

Generation, from the repository root (one command per source):

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 --output=tests/integration/fixtures/enumeration_composition/main.abc tests/integration/fixtures/enumeration_composition/main.js
```

Substitute `decision`, `local_transfer` or `set_equal` for both occurrences of
`main`. Compiler SHA-256:
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
SDK `toolchains/ark_disasm` SHA-256:
`a82e0c92a6726eb5c515f96c1731984e2a3938a6ce98dc540c7b5e7c08860676`.

| File | SHA-256 |
|---|---|
| main.js | f8fa5bdad42cb55b0e797af67895d6f6c7533ab07489dac38aa74f427892e091 |
| main.abc | 39054e3a5b96efb7b5fcff4ca56d69b614f815126c89ce1405d3a8a29fa78c4e |
| decision.js | a6415c0deeea07b9dcf4175709ba54430d077e1721aa18cbf04d0e7ca6b0c830 |
| decision.abc | 23b8c64789b76a74d7ad853145b279b79bb96e0aef9244fd99cb1a51bcd5caff |
| local_transfer.js | 2b1b25ae4a87dcb2e6c5c4987b6cf78150f0893d2ad1cdb6f9578b1f0d74baa1 |
| local_transfer.abc | 554ca9bbcad4e9d039cac4e5566f8ee2236d1e469b6a6855d818c9d25226e992 |
| set_equal.js | 78757a171ab5dcf5536d78ed30b600798b29adb9d46567fdbceb9acc746acbf0 |
| set_equal.abc | 31f929d0aab422942ae699ad62d803d18c9bdcf04624fc354718427ab2e0b660 |

The Node test runs **318** comparisons, including receiver-sensitive getters,
custom symbol iteration, throwing callbacks, close failures suppressed by body
throws, retained truthy objects and empty/last-value cases. The existing
`iteration_context_cli_test` additionally covers **20** cases against its own
independent source. These are ordinary-call Node oracles, not Ark runtime runs.

`tests/tools/grouping_bytecode.py` was also run on each original/emitted pair,
at both default optimization and O0. All 16 compilations and disassemblies
succeed after applying the same identifier-export-alias AST adaptation to each
side independently. Strict comparison reports **4 differing records per mode**,
not equality passes. No register/branch/handler normalization or allowance was
added. Detailed differences (private locals/phi copies, branch routing through
an existing jump, removal of an inert strict comparison, lexical names/guards)
are explained in
[o32-cluster.md](../../../../docs/design/source-recovery/o32-cluster.md).
