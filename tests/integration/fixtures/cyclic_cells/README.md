# Cyclic local-cell initialization (Round 64)

The source is a test oracle, never an engine input. `cells` allocates a captured
cell before a loop, reads it through the closure and directly in later blocks,
and performs subsequent writes. `activation` checks fresh loop activations and
escaping closures. The test verifies lexical allocation/load/store and function
creation in the ABC before comparing engine-generated source with Node.

Compiler: local DevEco OpenHarmony SDK es2abc, ABC 24.0.0.0.

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 --record-name=cyclic_cells \
  --source-file=cells.js --output tests/integration/fixtures/cyclic_cells/cells.abc \
  tests/integration/fixtures/cyclic_cells/cells.js
```

SHA-256:

- cells.js: `861aa49d88dde452218800a771e679a95c0eef4b22e84dafe4604893425552cd`
- cells.abc: `3ebc1efec6c4accd7e0c0c6f540af3956494e7e4c99f5f9f98eb1c7d385694fd`

The differential covers zero/one/multiple iterations, init/write side effects,
initializer and later-write exceptions, last values and per-activation closure
identity. Compare values, exception constructors and full effect traces, not
exception messages or stacks. No throwing-finally source oracle is used.
Mutations change the initializer, the written value and the effect kind. Raw-IR
tests separately reject missing initialization and a fresh allocation bypassing
the store, and independently reject forged cell-initialization witnesses.
