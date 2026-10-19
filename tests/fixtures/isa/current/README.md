# Current ISA evidence fixtures (increment 3)

This directory is an instruction-coverage corpus, **not** an executable semantic
oracle. It covers the 127 non-deprecated variants classified Unmeasured at
`7d97d2a6`. The 47 deprecated variants are deferred, not classified as dead.

`manifest.json` records the complete input/output SHA-256 values, compiler and
ark_disasm identities, exact commands and compiler stdout/stderr. The 24 source
configurations are compiled at `--opt-level=0` and at the default level. Their
48 compilations observe 82 of the selected variants. The 45 others remain
**not observed**, with no assertion of exhaustive frontend non-emittability.

Reproduce from the repository root, with the existing `build/regression` tools:

```sh
python3 tests/isa/current_variants_test.py --generate
python3 tests/isa/current_variants_test.py
python3 tests/isa/coverage_matrix.py --root . --output /private/tmp/isa3-coverage
python3 tests/isa/current_report.py --coverage /private/tmp/isa3-coverage
```

Compiler:
`/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc`

SHA-256: `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.
The tests recompile all 48 commands with temporary output locations and require
identical ABC hashes. Source filenames/paths are deliberately retained because
they affect ABC identity. `.ets --extension=ts` is used for typed Sendable fields.

The sources cover arithmetic/constants, object protocols, namespace/dynamic/lazy
imports, scripts/globals, private fields, ordinary and async generators, rest and
wide arguments/cells/modules/branches, Sendable/shared scopes, debug evaluation,
hot reload, API12 and callable-name mode. The generated current-variant report
links each observed variant to its source and each direct emitter to the pinned
frontend. Hot reload alone did not emit patch-var operations: the additional-var
symbol-table workflow remains a probe gap. No claim of an impossible opcode is
made from that negative result.

All 127 variants additionally have one constructed ABC each, generated with the
test-only two-pass writer. They contain one selected instruction, undefined acc,
a real frame and pool, and a return. Branch deltas are label-resolved; packed
nibbles and prefix sizes are derived from the generated catalog. Every file is
cross-checked by SDK `ark_disasm` and the engine decoder. Its production raw-lift
result is recorded in the manifest (43 successful lifts, 84 typed unsupported
refusals). The registers, empty tagged literal, and method self-reference are
**not** a valid execution protocol for arbitrary operations. These specimens
must never be executed or treated as a Node/Ark runtime pass. No source sidecar
is supplied to the production engine.

The tests cover builder 200/201/400/800 scaling and exact allocation boundaries,
file and compiler hash changes, format/prefix evidence (including secondary
opcode zero), and the current lift distributions. Every Node / Ark runtime /
Ark round-trip oracle remains absent. Compiler reproduction is not a decompile
round-trip or a VM execution oracle.
