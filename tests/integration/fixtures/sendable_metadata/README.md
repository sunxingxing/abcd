# Sendable/shared metadata fixtures (Round 74 increment I)

These are independently authored evaluation inputs, never engine inputs other
than the ABC files. `classes.ts` covers a closed Sendable class with two ordered
[[Set]] writes, primitive/union/GENERIC field masks, and an instance/static split.
`shared.ts` covers a real shared flag, import request and exported instance.

Compiler: `/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc`, bytecode version 24.0.0.0.
Binary SHA-256: `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
Pinned semantic references: OH7 ets_frontend c754cf6300873cdd8cdb71c7340a98abfd885389,
ets_runtime f04900cf951c66c2ea18b2bab5b591d5336c34b9.

Run from the repository root (COMPILER is the DevEco path above):

```sh
$COMPILER --module --merge-abc --extension=ts --opt-level=0 --record-name=sendable_classes --output=tests/integration/fixtures/sendable_metadata/classes.abc tests/integration/fixtures/sendable_metadata/classes.ts
$COMPILER --module --merge-abc --extension=ts --opt-level=0 --record-name=shared_module --output=tests/integration/fixtures/sendable_metadata/shared.abc tests/integration/fixtures/sendable_metadata/shared.ts
```

Do not enable `--use-define-semantic`. Field descriptions have no initializer;
the original constructor assignments remain [[Set]]. The C++ metadata test
checks actual decoded opcode/literal references and independently rejects
mutated owner, pool, masks, placement and field-buffer evidence. The CLI test
compiles 200/201/400/800 fields and exercises exact byte/work admission bounds.
It checks both production contracts still refuse, with identical first causes
and CLI/manifest witnesses. Increment I does not publish source. Engine-output
Node differentials and target recompilation are increment II acceptance, not
claimed here. Node cannot validate shared heap, field type enforcement or
cross-thread identity.

SHA-256:

- `classes.abc`: `835f9dd017836fe2c05c63aeee80c4dea0d891c87cc15b76d6b712ee52f7304d`
- `classes.ts`: `afc30f6d946c28a6530a40eb0db4fd4c0f81332977b862b9a9e027654e8e65e7`
- `shared.abc`: `e7f9fc7e4df362712018cc86b98d98bfce3561dc5f1e4a3a3e2838f6b731a8b4`
- `shared.ts`: `2cad497014a56a5d51e65c2866adb598425390adc7e10e04c80f6cad9657caff`
