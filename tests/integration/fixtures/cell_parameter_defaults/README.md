# Cell-backed captured default parameter (Round 77, increments I/II)

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

Increment III publishes the native default. The fixture checks local parameter
store/load at depth 0, the distinct outer load at depth 1, and the named hole
guard propagating to the caller. Incoming CapturedDepth 0 is relative to the
function entry, not the instruction's lexical depth after its local allocation.

`behavior.js` adds reassignment of the outer cell between calls, closure reads
and writes of the body parameter cell, per-call isolation, missing and explicit
undefined, null/false/0, and an early call before the outer declaration. The
provided-argument early call succeeds without reading the outer cell; the
missing-argument call throws ReferenceError before any body log. Node compares
exception constructors and traces, never messages/stacks. Mutations initialize
the outer cell too early or read it on the provided path and must be detected.

Compile this second fixture with the same compiler/options:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 --output=tests/integration/fixtures/cell_parameter_defaults/behavior.abc tests/integration/fixtures/cell_parameter_defaults/behavior.js
```

- behavior.js SHA-256: `84ef1ebe50d27ae33bb111206ed842c29f57cc6414871ece4d6b2a33fef43194`
- behavior.abc SHA-256: `e74e80583c03f105e5e2fa456654a2f7940bc1a847c9ba6a3dd23250f6691088`

Both contracts export both fixtures. Node executes engine-produced `.mjs` and
compares the behavior fixture with its independent source oracle. The compiler
round-trip recompiles the engine's `defaults` function text unchanged. The
pinned compiler does not parse string-valued export aliases, so the harness
unquotes only identifier-valued export aliases at that syntax boundary. It
checks DEFINEFUNC length 2, conditional default initialization, distinct outer
and body cells, the body closure's capture and the caller-propagating HoleGuard
in the recompiled ABC. It does not require that the recompiled record can itself
be fully decompiled: the compiler's lowering of the inferred-name wrapper adds
an independently unsupported operation. This is metadata/bytecode round-trip
acceptance plus Node execution of the first engine output, not a second-output
semantic claim.
