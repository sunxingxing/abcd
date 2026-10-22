# Measured ISA ledger

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
* `SupportedVerified` requires raw lift success, current complete recovery, and
  an imported successful occurrence oracle in that same ISA profile. A green
  method/record or registry label alone never suffices. The 332-row display is
  existential and lists `verified_profiles`; `profile_rows` is authoritative for
  profile-specific coverage. Other profiles keep their own classification.
* `node` records a complete record tested by a registered differential suite,
  not a claim that every instruction/branch executed. `ark_round_trip` records
  a separate matched recompiled event. `ark_runtime` remains null: no Ark VM is
  available. These layers must not be described as interchangeable.
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

`coverage_manifest.json` registers the 28 constructed extended-branch refusals.
The ledger runs `isa_constructed_branches_test` before accepting NonExecutable
classification, then independently checks the runtime citation hash, fixture
hash, raw typed refusal and both record refusals. Only the measured profile is
classified; no runtime/Node pass is imported for these instructions. Registry checks reject stale hashes, missing decoded opcodes, and
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

The generated large JSON remains untracked (`build/isa-coverage/coverage.json`);
copy `matrix.json` there after a complete run. The Markdown variant table is
checked in at `docs/design/source-recovery/isa-coverage.md`.

The generated ledger classifies the 28 non-executable variants only in the
measured 12.0.6 profile; it does not transfer a fixture's observation to other
profiles. `validate_non_executable` also rejects a successful lift, a mismatched
runtime-evidence ID, missing record refusal, or a stale citation hash. The raw
probe includes the fixed-size `non_executable` witness on its refusal row.

## Deprecated-profile evidence

`deprecated_variants.json` records all 47 deprecated variants across the six
explicit `builder.Profile` layouts. Run `ctest --test-dir build/regression -R
isa_deprecated_evidence_test --output-on-failure`; regenerate the 282 ABCs and
measurements with `python3 tests/isa/deprecated_variants_test.py --generate`.
The test cross-checks SDK `ark_disasm`, `abc_decode`, and production per-op raw
lifting/recovery. The registry's Unmeasured assertion does not grant support;
the matrix classifies each real `(profile, variant)` observation separately.
No runtime pass is claimed. Handler, baseline and AOT metadata citations and
known path differences are in
[the evidence note](../../docs/design/source-recovery/isa-deprecated-evidence.md).

## Occurrence oracle transactions

`coverage_manifest.json::oracle_producers` names existing CTest differentials.
The matrix runs them serially in a fresh `oracle-run-*` directory with
`ABCD_ISA_ORACLE_DIR` set. The shared test-only
`tests/support/isa/oracle_evidence.{py,mjs}` writer is called **after** differential
assertions. CTest success, a non-skipped JUnit case, a positive executed-case
count and a committed evidence file are all required. Tests behave normally
without the environment variable. No production target imports this code.

Evidence retains ABC, emitted source, evaluation-only source, manifest and
SHA-256 identities for the producer, writer, production engine and raw probe;
Node version, tested contract and approved observation exclusions are recorded.
For a multi-record input, the producer must explicitly list covered record IDs.
It cannot certify unrelated complete records. The occurrence stream joins on
`(ABC sha256, ISA profile, record, method, original PC, contract)`, then checks
the current decoded variant, raw semantic family and complete recovery again.
Skipped/refused/stale/wrong-profile evidence never counts as a pass.

The round-trip writer uses pinned es2abc at `--opt-level=0`, SDK ark_disasm and
the **same production probe** on the recompiled ABC. Method identity must be
unique on both sides (encoded name, kind, formal slots), and the full ordered
semantic-family sequence, refined by nested typed expression/operator kinds,
must agree. Correspondence is positional only after those checks. No LCS,
register renaming, width equality or branch/exception normalization is used to
manufacture a match. Method ambiguities, sequence differences and compilation
failures are retained in `round-trip.json`, with exact commands/tool hashes;
they remain null oracles. This is deliberately conservative and is weaker than
whole-bytecode equality. The existing real-lexer string-export adaptation is
applied only at the SDK compilation boundary and logged. An ordinal match is
not an Ark execution oracle.

Transactions are streamed as gzip JSONL and joined through a SQLite index with
16 MiB page cache. Retained round-trip rows have a prepaid 64 MiB byte allowance,
a 65,536-event per-method cap, and a 1 MiB input-row cap; admission precedes
retention. Exhaustion records a resource mismatch, never round-trip success.
Importer JSON metadata is capped at16 MiB and each occurrence at1 MiB.
`isa_oracle_import_test` covers stale artifacts, incomplete recovery, refusal,
wrong identity/profile, skipped Node evidence, forged round trips and exact
200/201/400/800 event-budget boundaries.

`isa_verified_variants_test` checks SDK-generated XOR/exponent/index-lookup
sources at default/O0, and constructed current/deprecated indexed Get and strict
Set encodings. The compiler currently emits by-value operations for these
numeric access sources, so the distinct index encodings use the existing typed
ABC builder rather than claiming an unobserved compiler emission. SDK decode,
production `abc_decode`, complete recovery and independent Node traces are
required. A source index mutation must change the program and be observed.

## Round-2 recovery fixtures

`isa_recovery_variants_test` adds deterministic default/O0 SDK artifacts under
`tests/fixtures/isa/recovery/`; its manifest pins source/compiler/disassembler
hashes, commands, observed opcodes and the exact current recovery outcome.
Only a complete record that passes `isa_recovery.mjs` supplies occurrence
oracles. Optimized-body refusals are asserted separately and supply no oracle.
Primitive-key computed super and BigInt pool literals are independently checked
by `super_read_test` and `isa_pool_super_test`, including exact resource limits.
The semantic restrictions, remaining lift-ok targets and runtime differences
requiring decisions are in
[isa-coverage-round2.md](../../docs/design/source-recovery/isa-coverage-round2.md).
No new contract or LDNEWTARGET behavior is enabled.

## Round 3 / LanguageSourceV7

Latest measured variant display: **132 SupportedVerified / 62 SupportedUnverified /
110 Unmeasured / 28 NonExecutable** (`build/review/isa-r3/matrix.json`). Fifteen
variants moved forward and none backward at the variant or profile level. The
new rows verify separately in 12.0.6 and 24.0.0; no evidence is transferred.

`isa_native_frame_test` registers 30 constructed exported-function ABCs and two
SDK default/O0 fixtures. Both the SDK disassembler and engine decoder must accept
them. Its shared occurrence writer runs after 244 Node differential cases.
New-target interpreter differences are explicitly excluded under V7; Ark runtime
is null. Literal descriptors, strict receiver behavior, getter/setter/key effects,
freshness and rest copying remain compared. Hand-written evaluation oracles are
never engine inputs. Builder function kinds/exports are test-only typed layout.

V7 has four separate approved predicates: tagged templates, object spread, object
rest and new.target. Only new.target is consumed this round. The other three
remain recovery gaps until their construction/event certificates exist; registry
labels cannot authorize them. Implicit-this Get/strict Set uses ordinary member
operations and has no new premise. The C++ owner/operand verifier tests mutate
receiver/key/scope and exercise exact resource boundaries, including split blocks.

Strict `grouping_bytecode.py` reference/recovery compilation in both modes yields
30 exact records, two LDFUNCTION differences, zero unverified. Those differences
are the oracle's unchanged module binding versus the actual immutable function
self value; see the [round note](../../docs/design/source-recovery/isa-coverage-round3.md).
The occurrence writer retains 210 unmatched events as null round-trip evidence,
and imports 31 strict matched events. Full-file equality, event-family evidence
and Node differential remain distinct.
