# Property instruction evidence (ISA increment 2, partial)

These are decode/emission fixtures, **not semantic-support fixtures**. No engine
output or Ark runtime oracle has passed for the 21 target variants. The current
production lifter refuses every target instruction with UnsupportedInstruction;
the test explicitly distinguishes that from NonExecutable. No runtime execution
of the constructed methods is claimed (their arbitrary receivers need not meet
runtime preconditions).

Pinned DevEco dynamic compiler:
`/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc`.
SHA-256: `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

Regenerate deterministically from repository root:

```
python3 tests/isa/property_variants_test.py --generate
```

The generator writes `properties.js` (evaluation-only), then runs:

```
es2abc --module --merge-abc --opt-level=0 --output=tests/fixtures/isa/properties/o0.abc tests/fixtures/isa/properties/properties.js
es2abc --module --merge-abc --output=tests/fixtures/isa/properties/default.abc tests/fixtures/isa/properties/properties.js
```

`manifest.json` records every ABC SHA-256, source SHA-256, compiler SHA-256,
actual decoded emitted variants and options. Each of the 21 other ABCs is a
single-method module produced by `tests/support/abc/builder.py`, using the real
generated encoding catalog. The engine decoder and SDK `ark_disasm` check all
23 ABCs. The test also exercises 200/201/400/800 instructions and exact builder
budget boundaries; this is not a new-lifter resource test.

The source exercises computed super reads, named/computed super writes and
anonymous function name-setting at both cache widths. `IsLegalNameFormat` in
the pinned frontend requires both a dot and backslash for the non-computed
name-setting path. Repeated sites exercise cache widening only; no production
admission depends on this layout.

The source also tests numeric bracket assignments at 0, 65535, 65536, 2^31 and
2^32-1 and ordinary `this.p` / `this[k]`: the compiler emits ordinary
`STOBJBYVALUE` / `LDOBJBYNAME` / `LDOBJBYVALUE` / `STOBJBYNAME`, not the target
indexed/this forms. Lack of emission in these probes is not proof of runtime
non-executability. All 21 handlers are implemented.

See `docs/design/source-recovery/isa-properties-runtime-evidence.md` for the
runtime semantics and unresolved source-projection obligations. Node, Ark
round-trip and Ark runtime oracle statuses intentionally remain absent.
