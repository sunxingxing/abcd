# Phased reachability

Independent source-language oracle. The engine receives only `reachability.abc`.
Generated with DevEco Studio's bundled es2abc on 2026-10-13:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 --record-name reachability --output tests/integration/fixtures/phased_reachability/reachability.abc tests/integration/fixtures/phased_reachability/reachability.ts
```

SHA-256:

- Compiler: `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`
- `reachability.ts`: `2a843835a42bcac21231d8dcdd4752602c38b6a810dd900f6e85016c0d49ea00`
- `reachability.abc`: `00cc5b25c2840b4bce244d8c1df1352a0cc7a6c4455d2995987f317826b7ed8c`

`caught` contains a dead jump following a return and an exception-only handler.
The CLI test independently checks these bytecode features. They are fixture
assertions, not production admission patterns. Import unit tests additionally
cover dead writes, argument snapshots, cycles, dead exception edges, and dead
predecessors of live catch code under register/graph scaling.

Node compares values, exception identity, receiver identity and effect traces:
normal return, getter failure, call failure, recovery failure, and finally
failure. Removing recovery or duplicating cleanup must change observations.
This is not Ark VM execution or a whole-corpus claim.

Compiler limitation discovered while constructing the oracle: with this exact
compiler/options, `try { return input.run(); } finally { input.finish(); }`
places the inlined `finish()` inside the first protected interval. Its handler
rethrows into the outer cleanup handler, which calls `finish()` again. The
compiler's `--dump-assembly` prints both catchall intervals; decoded PCs 36/44
(finish lookup/call) dispatch to 52, and the throw at 52 dispatches to 59.
The pinned runtime's `Method::FindCatchBlock` selects the first covering try
(`references/openharmony/arkcompiler/ets_runtime/ecmascript/method.cpp:87`).
Recovered bytecode behavior therefore differs from that TS source if finish
throws. We do not reinterpret that bytecode as native finally or claim that
case passes source differential testing. This fixture places the return after
finally; cleanup failures, including identity and execution count, are tested.
