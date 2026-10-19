# Measured ISA ledger (increment 0)

This is test tooling, not an admission table. No production source is selected by
an opcode name, fixture path, classification, or diagnostic string.

Build `isa_coverage_probe` in `build/regression`, then run:

```sh
python3 tests/isa/coverage_matrix.py --root . --output /private/tmp/isa-coverage
```

The driver reads `harmony-exploration-2026-10-12`, the five paired ABCs under
`samples/abc_with_code`, and ABC fixtures under `tests/`. The user-owned
`tests/samples/big.abc` is excluded. Sources and paired-source text are not engine
inputs. Only ABC paths are passed to production tools.

For every input, `abc_structure` runs once under each requested contract, before
raw measurement. Recovery manifests are checked against the ABC fingerprint and
contract. The recovery cache belongs to a specific production binary and flag
set; use another output directory after changing that binary. Raw caches also
include the probe binary hash. A nonzero probe exit is recorded, never hidden as
an empty successful scan.

```sh
build/regression/isa_coverage_probe --catalog
build/regression/isa_coverage_probe input.abc previous/manifest.json v4/manifest.json
```

The second command streams JSONL directly, including record and reported method
failures. The manifest arguments are optional for raw-only use. The driver also
independently joins the recovery fields in its compressed occurrence stream.
The method status is `not_individually_reported` if production record recovery
does not report it separately. Record completion is not invented as a standalone
method result or a per-instruction semantic oracle.

## Evidence and classification

* `rows` contains all 332 variants, including unobserved ones.
* `profile_rows` contains the separate `(profile, opcode)` measurements. The
  variant display's `profiles` embeds these same rows. Never transfer success
  from one profile to another.
* `SupportedUnverified` means at least one raw lift succeeded and that occurrence
  belongs to a complete record. This is partial evidence, not universal opcode
  support or execution coverage. The per-profile lift distribution remains.
* `SupportedVerified` requires an occurrence-level oracle importer, which is not
  implemented in increment 0. All Node / Ark round-trip / Ark runtime columns
  are null. Existing green tests are not imported without occurrence evidence.
* `UnsupportedPremiseFree`, `NeedsDecision`, and `NonExecutable` require a reviewed
  semantic justification. Refusal alone never selects one of these categories.
* `Unmeasured` includes never-seen variants and measured variants lacking enough
  classification evidence. It does not mean the raw probe was not run.

`current_first_refusal` means a production first refusal located exactly at the
original method/PC of that instruction. It is NOT a claim that implementing that
opcode resolves the record: a supported call may carry a control/ownership
refusal. `unsupported_first_refusals` separately selects the typed semantic
unsupported-instruction cause. Unlocated/synthetic/unmatched failures remain in
`unattributed_first_refusals`; PC zero is a real location, not an absence default.

`coverage_manifest.json` is deliberately empty until fixture/oracle evidence is
registered. Registry checks reject stale hashes, missing decoded opcodes, and
skipped or undocumented oracle passes. Refusals cannot become verified support.

## Storage and work

The probe retains one immutable metadata index and one decoded method, releases
each method before the next, and streams one instruction JSON object at a time.
It uses the existing 128 MiB metadata / 64 MiB body / 16 MiB dispatch allowances;
annotation scratch is 1 MiB. Each supplied recovery manifest is limited to 1 MiB
on disk, with a separate 64 MiB DOM/index allowance. Register ranges remain
compact; they are never expanded into argument-sized arrays. Typed semantic
roles are serialized from the returned semantic variant, not reconstructed from
opcode names. `normal_commit` is a normal-success write; exception exits use the
original first-match dispatch and before-commit state.

The driver runs one child at a time and keeps per-variant counters and distinct
record sets, not per-instruction objects. Raw and enriched JSONL are gzip streams
in the output directory. Catalog grouping is indexed, not a variant-by-occurrence
cross product. Hashing uses 1 MiB chunks. Production recovery keeps its own
existing limits. The probe reports its measured RSS; on Darwin the shared process
limit helper explicitly reports that an address-space limit was not established.
