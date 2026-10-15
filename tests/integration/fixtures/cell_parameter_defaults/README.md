# Cell-backed captured default parameter (Round 77, increment I)

Source: `defaults.js`, authored for this test. `make` creates a local outer
binding; `f` has a third-parameter default reading that binding, and its returned
arrow captures the initialized parameter. These are two different storages.

Compiler: DevEco dynamic es2abc at
`/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc`.
Compiler SHA-256: `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

From the repository root:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 --output=tests/integration/fixtures/cell_parameter_defaults/defaults.abc tests/integration/fixtures/cell_parameter_defaults/defaults.js
```

SHA-256:

- JS: `046d55e3450e5cdb6628a58dff41b848fcc4380ffc7b6d250df3f1bfd44b2afc`
- ABC: `68dc6b8e2f8b61f0cbcae1dd7a91646c27f93b906703f65fe2d09b7926fc21ae`

The test obtains the method and original PCs from the engine's diagnostic, then
checks the actual ABC instructions: local parameter store/load at depth 0,
outer load at depth 1, named hole guard propagating to the caller, and the local
write-back. The incoming storage uses CapturedDepth 0: it is relative to function
entry, unlike the instruction's lexical depth after its local allocation.

Both contracts must keep FunctionAbiError::kLength and publish a verified joint
proof as diagnostics only. abc_semantic --record-source must agree with the
manifest. No JS function is published by this increment; this is not a Node
semantic differential or a recovered-signature round-trip. Those remain gated
on parameter-scope, family-binding and recovery integration in increments II/III.
