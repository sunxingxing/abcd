# Measured ISA coverage

## Increment 1b evidence (2026-10-19)

Baseline `845c786a` (the runtime-evidence commit after `59db43cc`). The large JSON
is generated at `build/isa-coverage/coverage.json`, outside version control;
reproduce with the command in [the tool README](../../../tests/isa/README.md).
The full streaming run is in `/private/tmp/isa1b-coverage-final`.

The 28 extended conditional variants move from Unmeasured to **NonExecutable**
only for their constructed, measured profile (12.0.6). Other unmeasured profile
rows stay Unmeasured. Variant-level totals are 128 SupportedUnverified,
176 Unmeasured, 28 NonExecutable, and zero in the other three classes. This is
not a new support claim. JEQZ/JNEZ semantics are unchanged.

Classification requires the [pinned runtime citation](isa-branches-runtime-evidence.md),
its hash, a registered constructed fixture hash, successful decoding, the typed
raw refusal, both production record refusals, and a passing constructed-fixture
suite (including SDK disassembly). The source compiler does not produce these
variants. Node, Ark runtime, and round-trip columns for them remain empty.
The independent positive controls and their ordinary-call Node comparisons are
specified in [the fixture README](../../../tests/fixtures/isa/branches/README.md);
those passes are not transferred to the refused variants.

## Increment 2 evidence slice (2026-10-19)

Baseline `5e057b6f`. [Property evidence](isa-properties-runtime-evidence.md)
adds 21 constructed ABCs plus two reproducible es2abc O0/default ABCs. All 21
handlers are implemented; none is NonExecutable. Ten variants occur in both
compiler outputs; eleven occur only in the constructed fixtures in this run.
All 21 still refuse raw lifting. This slice does not implement source support.

The run `/private/tmp/isa2-coverage` contains 237 ABCs / 941,774 decoded
occurrences / 189 observed variants. Class counts are unchanged: 128
SupportedUnverified, 176 Unmeasured, 28 NonExecutable, zero in the other classes.
Unmeasured is retained for these property variants despite measured decoding:
the missing semantic/source oracle evidence must not be inferred from runtime
implementation or frontend emission. No Node/Ark oracle pass is claimed.
JSON: `build/isa-coverage/coverage.json` (generated, untracked).
Both exploration flag sets are byte-identical to the saved increment-1b run,
including manifests; counts remain 526/620 and 573/620.

## Increment 2b raw lifting (2026-10-19, incomplete)

Baseline `163b91aa`; measurement includes the uncommitted worktree changes.
The four WITHNAMESET variants now lift with `PropertyNameSetting::kPropertyKey`.
This does not complete literal recovery: a located `kNameSettingProof` gate
prevents the name-neutral path from publishing them. The constructed-fixture
tests check raw roles, accumulator preservation and throwing effects. No Node,
Ark runtime or round-trip oracle pass is imported.

The streaming run `/private/tmp/isa2b-coverage` has 237 ABCs / 941,774 occurrences.
Classification remains 128 SupportedUnverified / 176 Unmeasured / 28
NonExecutable; all four WITHNAMESET variants remain Unmeasured. The remaining
source-proof boundary and the computed numeric-key discrepancy are documented
in [the runtime evidence](isa-properties-runtime-evidence.md).

Both exploration flag sets are byte-identical to `/private/tmp/isa2-coverage`,
including all manifests (3,635 previous-flags files and 3,776 V4 files).
No record status changes: 526/620 and 573/620. The underlying raw-lift evidence
changes for the four variants, not their source-support classification.

## Increment 2c: named string-key projection (2026-10-19)

Baseline `67083c01`; measurements include the current worktree implementation.
The new engine-output fixture completes under both contracts. Both
STOWNBYNAMEWITHNAMESET widths move from Unmeasured to SupportedUnverified in
profile 6 (24.0.0.0); the constructed profile-4 occurrences still refuse publication
and retain Unmeasured. Both computed-key variants remain Unmeasured and gated.
This does not transfer source support across profiles or across key domains.

`/private/tmp/isa2c-coverage` measures 238 ABCs and 942,564 instructions, with
no probe failures. Totals: **130 SupportedUnverified, 174 Unmeasured,
28 NonExecutable**, zero in the other three classes. The Node fixture passes
separately; the ledger still has no occurrence-level oracle importer, so its
Node/Ark columns remain empty and no row claims SupportedVerified.

The exploration outputs are byte-identical to the increment-2b measurement,
including manifests and partial records: 3,635 previous-flags files and 3,776
V4 files, with 526/620 and 573/620 complete. No exploration record status
changed. Commands, hashes and the conservative class/static boundary are in
[the fixture README](../../../tests/fixtures/isa/named_properties/README.md) and
[the proof note](isa-properties-runtime-evidence.md#increment-2c-string-key-native-naming-2026-10-19).

## Increment 3: current-variant evidence pass (2026-10-19)

Baseline `7d97d2a6`. The fixed selection is 127 current / 47 deprecated
Unmeasured variants. This increment audits the **127 current** variants;
**47 deprecated remain pending**, without presuming they are non-executable.
See the [complete current inventory and prioritized backlog](isa-current-evidence.md).

All 127 have constructed engine/SDK-disassembler witnesses (43 raw lifts,
84 typed UnsupportedInstruction refusals). Forty-eight reproducible es2abc
O0/default commands observe 82 selected variants; 45 remain not observed.
No additional current variant is non-executable on all pinned paths:
LDNEWTARGET is FATAL/stub in interpreter paths but implemented in baseline/AOT.
No production code, source policy or refusal table changes in this increment.
The test-only probe corrects prefix reporting for secondary opcode zero.

`/private/tmp/isa3-coverage`: **413 ABCs / 1,309,154 instructions**, no probe
failures. Class totals: **144 SupportedUnverified / 160 Unmeasured /
28 NonExecutable**, zero in the other classes. The fourteen newly classified
variants are TONUMBER, NOT, LDNAN, LDINFINITY, TYPEOF_IMM16, JMP/JEQZ/JNEZ_IMM32,
DEBUGGER, NOP, WIDE_NEWLEXENV, WIDE_NEWLEXENVWITHNAME, WIDE_LDLEXVAR and
WIDE_STLEXVAR. These are measurement gains, not new semantics. Oracle columns
remain empty; successful compilation and raw lifting are not VM/source oracles.

Both exploration outputs are byte-identical to the prior ledger run, including
manifests and partial files: **526/620** previous flags, **573/620** V4;
3,635 / 3,776 files, zero changes. Comparison evidence is
`/private/tmp/isa3-cohort-comparison.json`.

## Current refresh: activation-local staged literals (2026-10-19)

Regenerated with the working tree based on `66ed407c`, including the staged
activation proof. The generated JSON and binary hashes remain untracked at
`build/review/triage/staged-accepted-isa/matrix.json`. Historical sections above
retain their original measurements; the tables below are current.

The generator's `v4` result key/column is historical: this run uses
`--language-source`, which selects **V5**. Current totals are **145
SupportedUnverified / 159 Unmeasured / 28 NonExecutable**, zero in the other
classes. APPLY has measured supported occurrences following the previously
committed private-call-spread work. No new ISA semantics or oracle classification
is claimed by the staged-literal proof. The historical APPLY unsupported-first-
refusal rows are removed by real pipeline measurement, not manual filtering.

Generated by `tests/isa/coverage_matrix.py`. Oracle layers remain separate. NonExecutable requires a pinned citation and passing constructed-ABC refusal tests.

Engine: `66ed407c96940a875140c9cc5bac9af2d9082a6a`. Files: 417. Occurrences: 1309699.

Raw lift and record completion do not prove per-instruction source equivalence. Method status is unmeasured unless independently inspected.

| Opcode | Family | Occurrences | Measured profiles | Classification |
|---|---|---:|---|---|
| LDUNDEFINED | Constant | 23979 | 3, 4, 5, 6 | SupportedUnverified |
| LDNULL | Constant | 716 | 3, 4, 5, 6 | SupportedUnverified |
| LDTRUE | Constant | 2310 | 3, 4, 5, 6 | SupportedUnverified |
| LDFALSE | Constant | 4144 | 3, 4, 5, 6 | SupportedUnverified |
| CREATEEMPTYOBJECT | Creation | 1250 | 3, 4, 5, 6 | SupportedUnverified |
| CREATEEMPTYARRAY_IMM8 | Creation | 3414 | 3, 4, 5, 6 | SupportedUnverified |
| CREATEARRAYWITHBUFFER_IMM8_ID16 | Creation | 198 | 3, 4, 5, 6 | SupportedUnverified |
| CREATEOBJECTWITHBUFFER_IMM8_ID16 | Creation | 8280 | 3, 4, 5, 6 | SupportedUnverified |
| NEWOBJRANGE_IMM8_IMM8_V8 | Creation | 2357 | 3, 4, 5, 6 | SupportedUnverified |
| NEWLEXENV_IMM8 | Creation | 83 | 6 | SupportedUnverified |
| ADD2_IMM8_V8 | Binary | 6285 | 3, 4, 5, 6 | SupportedUnverified |
| SUB2_IMM8_V8 | Binary | 208 | 3, 4, 5, 6 | SupportedUnverified |
| MUL2_IMM8_V8 | Binary | 170 | 3, 4, 5, 6 | SupportedUnverified |
| DIV2_IMM8_V8 | Binary | 130 | 3, 4, 5, 6 | SupportedUnverified |
| MOD2_IMM8_V8 | Binary | 42 | 3, 4, 5, 6 | SupportedUnverified |
| EQ_IMM8_V8 | Binary | 1610 | 3, 4, 5, 6 | SupportedUnverified |
| NOTEQ_IMM8_V8 | Binary | 429 | 3, 4, 5, 6 | SupportedUnverified |
| LESS_IMM8_V8 | Binary | 211 | 3, 4, 5, 6 | SupportedUnverified |
| LESSEQ_IMM8_V8 | Binary | 50 | 3, 4, 5, 6 | SupportedUnverified |
| GREATER_IMM8_V8 | Binary | 146 | 3, 4, 5, 6 | SupportedUnverified |
| GREATEREQ_IMM8_V8 | Binary | 65 | 3, 4, 5, 6 | SupportedUnverified |
| SHL2_IMM8_V8 | Binary | 36 | 6 | SupportedUnverified |
| SHR2_IMM8_V8 | Binary | 5 | 5 | SupportedUnverified |
| ASHR2_IMM8_V8 | Binary | 10 | 5 | SupportedUnverified |
| AND2_IMM8_V8 | Binary | 16 | 5, 6 | SupportedUnverified |
| OR2_IMM8_V8 | Binary | 15 | 3, 4, 5, 6 | SupportedUnverified |
| XOR2_IMM8_V8 | Binary | 5 | 3, 4, 6 | Unmeasured |
| EXP_IMM8_V8 | Binary | 5 | 3, 4, 6 | Unmeasured |
| TYPEOF_IMM8 | Unary | 500 | 3, 4, 5, 6 | SupportedUnverified |
| TONUMBER_IMM8 | Unary | 11 | 3, 4, 6 | SupportedUnverified |
| TONUMERIC_IMM8 | Unary | 153 | 3, 4, 5, 6 | SupportedUnverified |
| NEG_IMM8 | Unary | 584 | 3, 4, 5, 6 | SupportedUnverified |
| NOT_IMM8 | Unary | 5 | 3, 4, 6 | SupportedUnverified |
| INC_IMM8 | Unary | 177 | 3, 4, 5, 6 | SupportedUnverified |
| DEC_IMM8 | Unary | 12 | 3, 4, 6 | SupportedUnverified |
| ISTRUE | Unary | 238 | 3, 4 | SupportedUnverified |
| ISFALSE | Unary | 835 | 3 | SupportedUnverified |
| ISIN_IMM8_V8 | Comparison | 366 | 3, 4, 5, 6 | SupportedUnverified |
| INSTANCEOF_IMM8_V8 | Comparison | 10 | 6 | SupportedUnverified |
| STRICTNOTEQ_IMM8_V8 | Comparison | 2422 | 3, 4, 5, 6 | SupportedUnverified |
| STRICTEQ_IMM8_V8 | Comparison | 2734 | 3, 4, 5, 6 | SupportedUnverified |
| CALLARG0_IMM8 | Call | 366 | 3, 4, 5, 6 | SupportedUnverified |
| CALLARG1_IMM8_V8 | Call | 839 | 3, 4, 5, 6 | SupportedUnverified |
| CALLARGS2_IMM8_V8_V8 | Call | 470 | 3, 4, 5, 6 | SupportedUnverified |
| CALLARGS3_IMM8_V8_V8_V8 | Call | 243 | 3, 4, 5, 6 | SupportedUnverified |
| CALLTHIS0_IMM8_V8 | Call | 44671 | 3, 4, 5, 6 | SupportedUnverified |
| CALLTHIS1_IMM8_V8_V8 | Call | 13359 | 3, 4, 5, 6 | SupportedUnverified |
| CALLTHIS2_IMM8_V8_V8_V8 | Call | 4624 | 3, 4, 5, 6 | SupportedUnverified |
| CALLTHIS3_IMM8_V8_V8_V8_V8 | Call | 795 | 3, 4, 5, 6 | SupportedUnverified |
| CALLTHISRANGE_IMM8_IMM8_V8 | Call | 423 | 3, 4, 5, 6 | SupportedUnverified |
| SUPERCALLTHISRANGE_IMM8_IMM8_V8 | Call | 394 | 3, 4, 5, 6 | SupportedUnverified |
| DEFINEFUNC_IMM8_ID16_IMM8 | Definition | 13008 | 3, 4, 5, 6 | SupportedUnverified |
| DEFINEMETHOD_IMM8_ID16_IMM8 | Definition | 3655 | 3, 4, 5, 6 | SupportedUnverified |
| DEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8 | Definition | 1436 | 3, 4, 5, 6 | SupportedUnverified |
| GETNEXTPROPNAME_V8 | ObjectAccess | 34 | 6 | SupportedUnverified |
| LDOBJBYVALUE_IMM8_V8 | ObjectAccess | 738 | 3, 4, 5, 6 | SupportedUnverified |
| STOBJBYVALUE_IMM8_V8_V8 | ObjectAccess | 405 | 3, 5, 6 | SupportedUnverified |
| LDSUPERBYVALUE_IMM8_V8 | ObjectAccess | 300 | 4, 6 | Unmeasured |
| LDOBJBYINDEX_IMM8_IMM16 | ObjectAccess | 1 | 4 | Unmeasured |
| STOBJBYINDEX_IMM8_V8_IMM16 | ObjectAccess | 2 | 4 | Unmeasured |
| LDLEXVAR_IMM4_IMM4 | ObjectAccess | 20479 | 3, 4, 5, 6 | SupportedUnverified |
| STLEXVAR_IMM4_IMM4 | ObjectAccess | 2865 | 3, 4, 5, 6 | SupportedUnverified |
| LDA_STR_ID16 | String | 20546 | 3, 4, 5, 6 | SupportedUnverified |
| TRYLDGLOBALBYNAME_IMM8_ID16 | ObjectAccess | 29721 | 3, 4, 5, 6 | SupportedUnverified |
| TRYSTGLOBALBYNAME_IMM8_ID16 | ObjectAccess | 7 | 4, 6 | Unmeasured |
| LDGLOBALVAR_IMM16_ID16 | ObjectAccess | 7 | 4, 6 | Unmeasured |
| LDOBJBYNAME_IMM8_ID16 | ObjectAccess | 68306 | 3, 4, 5, 6 | SupportedUnverified |
| STOBJBYNAME_IMM8_ID16_V8 | ObjectAccess | 6876 | 3, 4, 5, 6 | SupportedUnverified |
| MOV_V4_V4 | Move | 89339 | 3, 4, 5, 6 | SupportedUnverified |
| MOV_V8_V8 | Move | 8270 | 3, 4, 5, 6 | SupportedUnverified |
| LDSUPERBYNAME_IMM8_ID16 | ObjectAccess | 51 | 3, 6 | SupportedUnverified |
| STCONSTTOGLOBALRECORD_IMM16_ID16 | ObjectAccess | 3 | 4, 6 | Unmeasured |
| STTOGLOBALRECORD_IMM16_ID16 | ObjectAccess | 7 | 4, 6 | Unmeasured |
| LDTHISBYNAME_IMM8_ID16 | ObjectAccess | 2 | 4 | Unmeasured |
| STTHISBYNAME_IMM8_ID16 | ObjectAccess | 2 | 4 | Unmeasured |
| LDTHISBYVALUE_IMM8 | ObjectAccess | 2 | 4 | Unmeasured |
| STTHISBYVALUE_IMM8_V8 | ObjectAccess | 2 | 4 | Unmeasured |
| JMP_IMM8 | Jump | 5397 | 3, 4, 5, 6 | SupportedUnverified |
| JMP_IMM16 | Jump | 1371 | 3, 4, 5, 6 | SupportedUnverified |
| JEQZ_IMM8 | Jump | 5701 | 3, 4, 5, 6 | SupportedUnverified |
| JEQZ_IMM16 | Jump | 321 | 3, 4, 5, 6 | SupportedUnverified |
| JNEZ_IMM8 | Jump | 5100 | 3, 4, 5, 6 | SupportedUnverified |
| JSTRICTEQZ_IMM8 | Jump | 1 | 4 | NonExecutable |
| JNSTRICTEQZ_IMM8 | Jump | 1 | 4 | NonExecutable |
| JEQNULL_IMM8 | Jump | 1 | 4 | NonExecutable |
| JNENULL_IMM8 | Jump | 1 | 4 | NonExecutable |
| JSTRICTEQNULL_IMM8 | Jump | 1 | 4 | NonExecutable |
| JNSTRICTEQNULL_IMM8 | Jump | 1 | 4 | NonExecutable |
| JEQUNDEFINED_IMM8 | Jump | 1 | 4 | NonExecutable |
| JNEUNDEFINED_IMM8 | Jump | 1 | 4 | NonExecutable |
| JSTRICTEQUNDEFINED_IMM8 | Jump | 1 | 4 | NonExecutable |
| JNSTRICTEQUNDEFINED_IMM8 | Jump | 1 | 4 | NonExecutable |
| JEQ_V8_IMM8 | Jump | 1 | 4 | NonExecutable |
| JNE_V8_IMM8 | Jump | 1 | 4 | NonExecutable |
| JSTRICTEQ_V8_IMM8 | Jump | 1 | 4 | NonExecutable |
| JNSTRICTEQ_V8_IMM8 | Jump | 1 | 4 | NonExecutable |
| LDA_V8 | RegisterLoad | 315705 | 3, 4, 5, 6 | SupportedUnverified |
| STA_V8 | RegisterStore | 339347 | 3, 4, 5, 6 | SupportedUnverified |
| LDAI_IMM32 | Immediate | 12258 | 3, 4, 5, 6 | SupportedUnverified |
| FLDAI_IMM64 | Immediate | 198 | 3, 4, 5, 6 | SupportedUnverified |
| RETURN | Return | 13652 | 3, 4, 5, 6 | SupportedUnverified |
| RETURNUNDEFINED | Return | 15318 | 3, 4, 5, 6 | SupportedUnverified |
| GETPROPITERATOR | Iterator | 34 | 6 | SupportedUnverified |
| GETITERATOR_IMM8 | Iterator | 53 | 3, 5, 6 | SupportedUnverified |
| CLOSEITERATOR_IMM8_V8 | Iterator | 1 | 4 | Unmeasured |
| POPLEXENV | Constant | 104 | 3, 4, 5, 6 | SupportedUnverified |
| LDNAN | Constant | 7 | 3, 4, 6 | SupportedUnverified |
| LDINFINITY | Constant | 8 | 3, 4, 6 | SupportedUnverified |
| GETUNMAPPEDARGS | Constant | 12 | 3, 4, 6 | Unmeasured |
| LDGLOBAL | Constant | 249 | 3, 4, 6 | SupportedUnverified |
| LDNEWTARGET | Constant | 1 | 4 | Unmeasured |
| LDTHIS | Constant | 1 | 4 | Unmeasured |
| LDHOLE | Constant | 4525 | 3, 4, 5, 6 | SupportedUnverified |
| CREATEREGEXPWITHLITERAL_IMM8_ID16_IMM8 | Creation | 1 | 4 | Unmeasured |
| CREATEREGEXPWITHLITERAL_IMM16_ID16_IMM8 | Creation | 1 | 4 | Unmeasured |
| CALLRANGE_IMM8_IMM8_V8 | Call | 10 | 5, 6 | SupportedUnverified |
| DEFINEFUNC_IMM16_ID16_IMM8 | Definition | 1056 | 3, 5, 6 | SupportedUnverified |
| DEFINECLASSWITHBUFFER_IMM16_ID16_ID16_IMM16_V8 | Definition | 2522 | 6 | SupportedUnverified |
| GETTEMPLATEOBJECT_IMM8 | ObjectAccess | 5 | 3, 4, 6 | Unmeasured |
| SETOBJECTWITHPROTO_IMM8_V8 | ObjectAccess | 5 | 3, 4, 6 | Unmeasured |
| STOWNBYVALUE_IMM8_V8_V8 | ObjectAccess | 13 | 6 | SupportedUnverified |
| STOWNBYINDEX_IMM8_V8_IMM16 | ObjectAccess | 2092 | 3, 4, 5, 6 | SupportedUnverified |
| STOWNBYNAME_IMM8_ID16_V8 | ObjectAccess | 1465 | 3, 4, 5, 6 | SupportedUnverified |
| GETMODULENAMESPACE_IMM8 | ObjectAccess | 3 | 4, 6 | Unmeasured |
| STMODULEVAR_IMM8 | ObjectAccess | 1542 | 3, 4, 5, 6 | SupportedUnverified |
| LDLOCALMODULEVAR_IMM8 | ObjectAccess | 524 | 3, 4, 5, 6 | SupportedUnverified |
| LDEXTERNALMODULEVAR_IMM8 | ObjectAccess | 6360 | 3, 4, 5, 6 | SupportedUnverified |
| STGLOBALVAR_IMM16_ID16 | ObjectAccess | 13 | 4, 6 | Unmeasured |
| CREATEEMPTYARRAY_IMM16 | Creation | 2360 | 3, 5, 6 | SupportedUnverified |
| CREATEARRAYWITHBUFFER_IMM16_ID16 | Creation | 30 | 5, 6 | SupportedUnverified |
| CREATEOBJECTWITHBUFFER_IMM16_ID16 | Creation | 1402 | 3, 5, 6 | SupportedUnverified |
| NEWOBJRANGE_IMM16_IMM8_V8 | Creation | 22 | 5, 6 | SupportedUnverified |
| TYPEOF_IMM16 | Unary | 257 | 4, 6 | SupportedUnverified |
| LDOBJBYVALUE_IMM16_V8 | ObjectAccess | 10 | 3, 6 | SupportedUnverified |
| STOBJBYVALUE_IMM16_V8_V8 | ObjectAccess | 261 | 4, 6 | Unmeasured |
| LDSUPERBYVALUE_IMM16_V8 | ObjectAccess | 286 | 4, 6 | Unmeasured |
| LDOBJBYINDEX_IMM16_IMM16 | ObjectAccess | 1 | 4 | Unmeasured |
| STOBJBYINDEX_IMM16_V8_IMM16 | ObjectAccess | 2 | 4 | Unmeasured |
| LDLEXVAR_IMM8_IMM8 | ObjectAccess | 291 | 4, 5, 6 | SupportedUnverified |
| STLEXVAR_IMM8_IMM8 | ObjectAccess | 270 | 4, 5, 6 | SupportedUnverified |
| TRYLDGLOBALBYNAME_IMM16_ID16 | ObjectAccess | 845 | 3, 4, 5, 6 | SupportedUnverified |
| TRYSTGLOBALBYNAME_IMM16_ID16 | ObjectAccess | 1 | 4 | Unmeasured |
| STOWNBYNAMEWITHNAMESET_IMM8_ID16_V8 | ObjectAccess | 174 | 4, 6 | SupportedUnverified |
| MOV_V16_V16 | Move | 54632 | 6 | SupportedUnverified |
| LDOBJBYNAME_IMM16_ID16 | ObjectAccess | 42252 | 3, 4, 5, 6 | SupportedUnverified |
| STOBJBYNAME_IMM16_ID16_V8 | ObjectAccess | 80 | 3, 5, 6 | SupportedUnverified |
| LDSUPERBYNAME_IMM16_ID16 | ObjectAccess | 259 | 4, 6 | Unmeasured |
| LDTHISBYNAME_IMM16_ID16 | ObjectAccess | 2 | 4 | Unmeasured |
| STTHISBYNAME_IMM16_ID16 | ObjectAccess | 2 | 4 | Unmeasured |
| LDTHISBYVALUE_IMM16 | ObjectAccess | 2 | 4 | Unmeasured |
| STTHISBYVALUE_IMM16_V8 | ObjectAccess | 2 | 4 | Unmeasured |
| ASYNCGENERATORREJECT_V8 | ObjectAccess | 3 | 4, 6 | Unmeasured |
| JMP_IMM32 | Jump | 3 | 4, 6 | SupportedUnverified |
| STOWNBYVALUEWITHNAMESET_IMM8_V8_V8 | ObjectAccess | 90 | 4, 6 | Unmeasured |
| JEQZ_IMM32 | Jump | 1 | 4 | SupportedUnverified |
| JNEZ_IMM16 | Jump | 1093 | 3, 4, 5, 6 | SupportedUnverified |
| JNEZ_IMM32 | Jump | 3 | 4, 6 | SupportedUnverified |
| JSTRICTEQZ_IMM16 | Jump | 1 | 4 | NonExecutable |
| JNSTRICTEQZ_IMM16 | Jump | 1 | 4 | NonExecutable |
| JEQNULL_IMM16 | Jump | 1 | 4 | NonExecutable |
| JNENULL_IMM16 | Jump | 1 | 4 | NonExecutable |
| JSTRICTEQNULL_IMM16 | Jump | 1 | 4 | NonExecutable |
| JNSTRICTEQNULL_IMM16 | Jump | 1 | 4 | NonExecutable |
| JEQUNDEFINED_IMM16 | Jump | 1 | 4 | NonExecutable |
| JNEUNDEFINED_IMM16 | Jump | 1 | 4 | NonExecutable |
| JSTRICTEQUNDEFINED_IMM16 | Jump | 1 | 4 | NonExecutable |
| JNSTRICTEQUNDEFINED_IMM16 | Jump | 1 | 4 | NonExecutable |
| JEQ_V8_IMM16 | Jump | 1 | 4 | NonExecutable |
| JNE_V8_IMM16 | Jump | 1 | 4 | NonExecutable |
| JSTRICTEQ_V8_IMM16 | Jump | 1 | 4 | NonExecutable |
| JNSTRICTEQ_V8_IMM16 | Jump | 1 | 4 | NonExecutable |
| GETITERATOR_IMM16 | Iterator | 309 | 4, 6 | Unmeasured |
| CLOSEITERATOR_IMM16_V8 | Iterator | 1 | 4 | Unmeasured |
| LDSYMBOL | Constant | 1 | 4 | Unmeasured |
| ASYNCFUNCTIONENTER | Constant | 591 | 3, 4, 5, 6 | SupportedUnverified |
| LDFUNCTION | Constant | 1 | 4 | Unmeasured |
| DEBUGGER | Constant | 3 | 4, 6 | SupportedUnverified |
| CREATEGENERATOROBJ_V8 | Creation | 3 | 4, 6 | Unmeasured |
| CREATEITERRESULTOBJ_V8_V8 | Creation | 3 | 4, 6 | Unmeasured |
| CREATEOBJECTWITHEXCLUDEDKEYS_IMM8_V8_V8 | Creation | 5 | 3, 4, 6 | Unmeasured |
| NEWOBJAPPLY_IMM8_V8 | Creation | 3 | 4, 6 | Unmeasured |
| NEWOBJAPPLY_IMM16_V8 | Creation | 301 | 4, 6 | Unmeasured |
| NEWLEXENVWITHNAME_IMM8_ID16 | Creation | 1473 | 3, 4, 5, 6 | SupportedUnverified |
| CREATEASYNCGENERATOROBJ_V8 | Creation | 3 | 4, 6 | Unmeasured |
| ASYNCGENERATORRESOLVE_V8_V8_V8 | Creation | 13 | 4, 6 | Unmeasured |
| SUPERCALLSPREAD_IMM8_V8 | Call | 39 | 3, 4, 6 | SupportedUnverified |
| APPLY_IMM8_V8_V8 | Call | 21 | 4, 6 | SupportedUnverified |
| SUPERCALLARROWRANGE_IMM8_IMM8_V8 | Call | 1 | 4 | Unmeasured |
| DEFINEGETTERSETTERBYVALUE_V8_V8_V8_V8 | Definition | 2136 | 3, 4, 5, 6 | SupportedUnverified |
| DYNAMICIMPORT | ObjectAccess | 5 | 3, 4, 6 | Unmeasured |
| DEFINEMETHOD_IMM16_ID16_IMM8 | Definition | 1 | 4 | Unmeasured |
| RESUMEGENERATOR | ObjectAccess | 590 | 3, 4, 5, 6 | Unmeasured |
| GETRESUMEMODE | ObjectAccess | 590 | 3, 4, 5, 6 | Unmeasured |
| GETTEMPLATEOBJECT_IMM16 | ObjectAccess | 301 | 4, 6 | Unmeasured |
| DELOBJPROP_V8 | ObjectAccess | 9 | 6 | SupportedUnverified |
| SUSPENDGENERATOR_V8 | ObjectAccess | 586 | 3, 4, 5, 6 | Unmeasured |
| ASYNCFUNCTIONAWAITUNCAUGHT_V8 | ObjectAccess | 578 | 3, 4, 5, 6 | Unmeasured |
| COPYDATAPROPERTIES_V8 | ObjectAccess | 5 | 3, 4, 6 | Unmeasured |
| STARRAYSPREAD_V8_V8 | ObjectAccess | 342 | 3, 4, 6 | SupportedUnverified |
| SETOBJECTWITHPROTO_IMM16_V8 | ObjectAccess | 301 | 4, 6 | Unmeasured |
| STOWNBYVALUE_IMM16_V8_V8 | ObjectAccess | 1 | 4 | Unmeasured |
| STSUPERBYVALUE_IMM8_V8_V8 | ObjectAccess | 172 | 4, 6 | Unmeasured |
| STSUPERBYVALUE_IMM16_V8_V8 | ObjectAccess | 414 | 4, 6 | Unmeasured |
| STOWNBYINDEX_IMM16_V8_IMM16 | ObjectAccess | 2284 | 3, 5, 6 | SupportedUnverified |
| STOWNBYNAME_IMM16_ID16_V8 | ObjectAccess | 1 | 4 | Unmeasured |
| ASYNCFUNCTIONRESOLVE_V8 | ObjectAccess | 721 | 3, 4, 5, 6 | SupportedUnverified |
| ASYNCFUNCTIONREJECT_V8 | ObjectAccess | 591 | 3, 4, 5, 6 | SupportedUnverified |
| COPYRESTARGS_IMM8 | ObjectAccess | 118 | 3, 4, 5, 6 | SupportedUnverified |
| STSUPERBYNAME_IMM8_ID16_V8 | ObjectAccess | 174 | 4, 6 | Unmeasured |
| STSUPERBYNAME_IMM16_ID16_V8 | ObjectAccess | 412 | 4, 6 | Unmeasured |
| STOWNBYVALUEWITHNAMESET_IMM16_V8_V8 | ObjectAccess | 498 | 4, 6 | Unmeasured |
| LDBIGINT_ID16 | ObjectAccess | 5 | 3, 4, 6 | Unmeasured |
| STOWNBYNAMEWITHNAMESET_IMM16_ID16_V8 | ObjectAccess | 213 | 4, 6 | SupportedUnverified |
| NOP | Nop | 13 | 3, 4, 6 | SupportedUnverified |
| SETGENERATORSTATE_IMM8 | ObjectAccess | 15 | 4, 6 | Unmeasured |
| GETASYNCITERATOR_IMM8 | Iterator | 5 | 4, 6 | Unmeasured |
| LDPRIVATEPROPERTY_IMM8_IMM16_IMM16 | Iterator | 9 | 4, 6 | Unmeasured |
| STPRIVATEPROPERTY_IMM8_IMM16_IMM16_V8 | Iterator | 3 | 4, 6 | Unmeasured |
| TESTIN_IMM8_IMM16_IMM16 | Iterator | 3 | 4, 6 | Unmeasured |
| DEFINEFIELDBYNAME_IMM8_ID16_V8 | Iterator | 4386 | 3 | SupportedUnverified |
| DEFINEPROPERTYBYNAME_IMM8_ID16_V8 | Iterator | 5796 | 4, 5, 6 | SupportedUnverified |
| CALLTHIS0WITHNAME_IMM8_ID16_V8 | Call | 4325 | 6 | SupportedUnverified |
| CALLTHIS1WITHNAME_IMM8_ID16_V8_V8 | Call | 7852 | 6 | SupportedUnverified |
| CALLTHIS2WITHNAME_IMM8_ID16_V8_V8_V8 | Call | 2794 | 6 | SupportedUnverified |
| CALLTHIS3WITHNAME_IMM8_ID16_V8_V8_V8_V8 | Call | 610 | 6 | SupportedUnverified |
| CALLTHISRANGEWITHNAME_IMM8_IMM8_ID16_V8 | Call | 242 | 6 | SupportedUnverified |
| CALLRUNTIME_NOTIFYCONCURRENTRESULT_PREF_NONE | RuntimeCall | 31 | 6 | SupportedUnverified |
| DEPRECATED_LDLEXENV_PREF_NONE | Constant | 0 | none | Unmeasured |
| WIDE_CREATEOBJECTWITHEXCLUDEDKEYS_PREF_IMM16_V8_V8 | Creation | 3 | 4, 6 | Unmeasured |
| THROW_PREF_NONE | Throw | 2608 | 3, 4, 5, 6 | SupportedUnverified |
| CALLRUNTIME_DEFINEFIELDBYVALUE_PREF_IMM8_V8_V8 | RuntimeCall | 1225 | 3, 4, 6 | Unmeasured |
| DEPRECATED_POPLEXENV_PREF_NONE | Constant | 0 | none | Unmeasured |
| WIDE_NEWOBJRANGE_PREF_IMM16_V8 | Creation | 3 | 4, 6 | Unmeasured |
| THROW_NOTEXISTS_PREF_NONE | Throw | 5 | 4, 6 | Unmeasured |
| CALLRUNTIME_DEFINEFIELDBYINDEX_PREF_IMM8_IMM32_V8 | RuntimeCall | 9 | 6 | SupportedUnverified |
| DEPRECATED_GETITERATORNEXT_PREF_V8_V8 | Iterator | 0 | none | Unmeasured |
| WIDE_NEWLEXENV_PREF_IMM16 | Creation | 1 | 4 | SupportedUnverified |
| THROW_PATTERNNONCOERCIBLE_PREF_NONE | Throw | 9 | 3, 4, 6 | Unmeasured |
| CALLRUNTIME_TOPROPERTYKEY_PREF_NONE | RuntimeCall | 1 | 4 | Unmeasured |
| DEPRECATED_CREATEARRAYWITHBUFFER_PREF_IMM16 | Creation | 0 | none | Unmeasured |
| WIDE_NEWLEXENVWITHNAME_PREF_IMM16_ID16 | Creation | 3 | 4, 6 | SupportedUnverified |
| THROW_DELETESUPERPROPERTY_PREF_NONE | Throw | 1 | 4 | Unmeasured |
| CALLRUNTIME_CREATEPRIVATEPROPERTY_PREF_IMM16_ID16 | RuntimeCall | 3 | 4, 6 | Unmeasured |
| DEPRECATED_CREATEOBJECTWITHBUFFER_PREF_IMM16 | Creation | 0 | none | Unmeasured |
| WIDE_CALLRANGE_PREF_IMM16_V8 | Call | 3 | 4, 6 | Unmeasured |
| THROW_CONSTASSIGNMENT_PREF_V8 | Throw | 3 | 4, 6 | Unmeasured |
| CALLRUNTIME_DEFINEPRIVATEPROPERTY_PREF_IMM8_IMM16_IMM16_V8 | RuntimeCall | 7 | 4, 6 | Unmeasured |
| DEPRECATED_TONUMBER_PREF_V8 | Unary | 0 | none | Unmeasured |
| WIDE_CALLTHISRANGE_PREF_IMM16_V8 | Call | 1 | 4 | Unmeasured |
| THROW_IFNOTOBJECT_PREF_V8 | Throw | 1069 | 3, 5, 6 | SupportedUnverified |
| CALLRUNTIME_CALLINIT_PREF_IMM8_V8 | RuntimeCall | 4 | 4, 6 | Unmeasured |
| DEPRECATED_TONUMERIC_PREF_V8 | Unary | 0 | none | Unmeasured |
| WIDE_SUPERCALLTHISRANGE_PREF_IMM16_V8 | Call | 3 | 4, 6 | Unmeasured |
| THROW_UNDEFINEDIFHOLE_PREF_V8_V8 | Throw | 1 | 4 | Unmeasured |
| CALLRUNTIME_DEFINESENDABLECLASS_PREF_IMM16_ID16_ID16_IMM16_V8 | RuntimeCall | 19 | 6 | SupportedUnverified |
| DEPRECATED_NEG_PREF_V8 | Unary | 0 | none | Unmeasured |
| WIDE_SUPERCALLARROWRANGE_PREF_IMM16_V8 | Call | 1 | 4 | Unmeasured |
| THROW_IFSUPERNOTCORRECTCALL_PREF_IMM8 | Throw | 5421 | 3, 4, 5, 6 | SupportedUnverified |
| CALLRUNTIME_LDSENDABLECLASS_PREF_IMM16 | RuntimeCall | 5 | 4, 6 | Unmeasured |
| DEPRECATED_NOT_PREF_V8 | Unary | 0 | none | Unmeasured |
| WIDE_LDOBJBYINDEX_PREF_IMM32 | ObjectAccess | 1 | 4 | Unmeasured |
| THROW_IFSUPERNOTCORRECTCALL_PREF_IMM16 | Throw | 1 | 4 | Unmeasured |
| CALLRUNTIME_LDSENDABLEEXTERNALMODULEVAR_PREF_IMM8 | RuntimeCall | 264 | 6 | SupportedUnverified |
| DEPRECATED_INC_PREF_V8 | Unary | 0 | none | Unmeasured |
| WIDE_STOBJBYINDEX_PREF_V8_IMM32 | ObjectAccess | 2 | 4 | Unmeasured |
| THROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16 | Throw | 14720 | 3, 4, 5, 6 | SupportedUnverified |
| CALLRUNTIME_WIDELDSENDABLEEXTERNALMODULEVAR_PREF_IMM16 | RuntimeCall | 265 | 4, 6 | Unmeasured |
| DEPRECATED_DEC_PREF_V8 | Unary | 0 | none | Unmeasured |
| WIDE_STOWNBYINDEX_PREF_V8_IMM32 | ObjectAccess | 1 | 4 | Unmeasured |
| CALLRUNTIME_NEWSENDABLEENV_PREF_IMM8 | RuntimeCall | 3 | 4, 6 | Unmeasured |
| DEPRECATED_CALLARG0_PREF_V8 | Call | 0 | none | Unmeasured |
| WIDE_COPYRESTARGS_PREF_IMM16 | ObjectAccess | 3 | 4, 6 | Unmeasured |
| CALLRUNTIME_WIDENEWSENDABLEENV_PREF_IMM16 | RuntimeCall | 1 | 4 | Unmeasured |
| DEPRECATED_CALLARG1_PREF_V8_V8 | Call | 0 | none | Unmeasured |
| WIDE_LDLEXVAR_PREF_IMM16_IMM16 | ObjectAccess | 285 | 4, 6 | SupportedUnverified |
| CALLRUNTIME_STSENDABLEVAR_PREF_IMM4_IMM4 | RuntimeCall | 3 | 4, 6 | Unmeasured |
| DEPRECATED_CALLARGS2_PREF_V8_V8_V8 | Call | 0 | none | Unmeasured |
| WIDE_STLEXVAR_PREF_IMM16_IMM16 | ObjectAccess | 285 | 4, 6 | SupportedUnverified |
| CALLRUNTIME_STSENDABLEVAR_PREF_IMM8_IMM8 | RuntimeCall | 1 | 4 | Unmeasured |
| DEPRECATED_CALLARGS3_PREF_V8_V8_V8_V8 | Call | 0 | none | Unmeasured |
| WIDE_GETMODULENAMESPACE_PREF_IMM16 | ObjectAccess | 3 | 4, 6 | Unmeasured |
| CALLRUNTIME_WIDESTSENDABLEVAR_PREF_IMM16_IMM16 | RuntimeCall | 1 | 4 | Unmeasured |
| DEPRECATED_CALLRANGE_PREF_IMM16_V8 | Call | 0 | none | Unmeasured |
| WIDE_STMODULEVAR_PREF_IMM16 | ObjectAccess | 533 | 4, 6 | Unmeasured |
| CALLRUNTIME_LDSENDABLEVAR_PREF_IMM4_IMM4 | RuntimeCall | 1 | 4 | Unmeasured |
| DEPRECATED_CALLSPREAD_PREF_V8_V8_V8 | Call | 0 | none | Unmeasured |
| WIDE_LDLOCALMODULEVAR_PREF_IMM16 | ObjectAccess | 265 | 4, 6 | Unmeasured |
| CALLRUNTIME_LDSENDABLEVAR_PREF_IMM8_IMM8 | RuntimeCall | 1 | 4 | Unmeasured |
| DEPRECATED_CALLTHISRANGE_PREF_IMM16_V8 | Call | 0 | none | Unmeasured |
| WIDE_LDEXTERNALMODULEVAR_PREF_IMM16 | ObjectAccess | 529 | 4, 6 | Unmeasured |
| CALLRUNTIME_WIDELDSENDABLEVAR_PREF_IMM16_IMM16 | RuntimeCall | 1 | 4 | Unmeasured |
| DEPRECATED_DEFINECLASSWITHBUFFER_PREF_ID16_IMM16_IMM16_V8_V8 | Definition | 0 | none | Unmeasured |
| WIDE_LDPATCHVAR_PREF_IMM16 | ObjectAccess | 1 | 4 | Unmeasured |
| CALLRUNTIME_ISTRUE_PREF_IMM8 | RuntimeCall | 1882 | 4, 5, 6 | SupportedUnverified |
| DEPRECATED_RESUMEGENERATOR_PREF_V8 | ObjectAccess | 0 | none | Unmeasured |
| WIDE_STPATCHVAR_PREF_IMM16 | ObjectAccess | 1 | 4 | Unmeasured |
| CALLRUNTIME_ISFALSE_PREF_IMM8 | RuntimeCall | 2467 | 4, 5, 6 | SupportedUnverified |
| DEPRECATED_GETRESUMEMODE_PREF_V8 | ObjectAccess | 0 | none | Unmeasured |
| WIDE_CALLTHISRANGEWITHNAME_PREF_IMM16_ID16_V8 | Call | 3 | 4, 6 | Unmeasured |
| CALLRUNTIME_LDLAZYMODULEVAR_PREF_IMM8 | RuntimeCall | 364 | 6 | SupportedUnverified |
| DEPRECATED_GETTEMPLATEOBJECT_PREF_V8 | ObjectAccess | 0 | none | Unmeasured |
| CALLRUNTIME_WIDELDLAZYMODULEVAR_PREF_IMM16 | RuntimeCall | 265 | 4, 6 | Unmeasured |
| DEPRECATED_DELOBJPROP_PREF_V8_V8 | ObjectAccess | 0 | none | Unmeasured |
| CALLRUNTIME_LDLAZYSENDABLEMODULEVAR_PREF_IMM8 | RuntimeCall | 259 | 4, 6 | Unmeasured |
| DEPRECATED_SUSPENDGENERATOR_PREF_V8_V8 | ObjectAccess | 0 | none | Unmeasured |
| CALLRUNTIME_WIDELDLAZYSENDABLEMODULEVAR_PREF_IMM16 | RuntimeCall | 265 | 4, 6 | Unmeasured |
| DEPRECATED_ASYNCFUNCTIONAWAITUNCAUGHT_PREF_V8_V8 | ObjectAccess | 0 | none | Unmeasured |
| CALLRUNTIME_SUPERCALLFORWARDALLARGS_PREF_V8 | RuntimeCall | 87 | 5, 6 | SupportedUnverified |
| DEPRECATED_COPYDATAPROPERTIES_PREF_V8_V8 | ObjectAccess | 0 | none | Unmeasured |
| CALLRUNTIME_LDSENDABLELOCALMODULEVAR_PREF_IMM8 | Throw | 255 | 4, 6 | Unmeasured |
| DEPRECATED_SETOBJECTWITHPROTO_PREF_V8_V8 | ObjectAccess | 0 | none | Unmeasured |
| CALLRUNTIME_WIDELDSENDABLELOCALMODULEVAR_PREF_IMM16 | Throw | 267 | 4, 6 | Unmeasured |
| DEPRECATED_LDOBJBYVALUE_PREF_V8_V8 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_LDSUPERBYVALUE_PREF_V8_V8 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_LDOBJBYINDEX_PREF_V8_IMM32 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_ASYNCFUNCTIONRESOLVE_PREF_V8_V8_V8 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_ASYNCFUNCTIONREJECT_PREF_V8_V8_V8 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_STLEXVAR_PREF_IMM4_IMM4_V8 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_STLEXVAR_PREF_IMM8_IMM8_V8 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_STLEXVAR_PREF_IMM16_IMM16_V8 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_GETMODULENAMESPACE_PREF_ID32 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_STMODULEVAR_PREF_ID32 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_LDOBJBYNAME_PREF_ID32_V8 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_LDSUPERBYNAME_PREF_ID32_V8 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_LDMODULEVAR_PREF_ID32_IMM8 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_STCONSTTOGLOBALRECORD_PREF_ID32 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_STLETTOGLOBALRECORD_PREF_ID32 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_STCLASSTOGLOBALRECORD_PREF_ID32 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_LDHOMEOBJECT_PREF_NONE | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_CREATEOBJECTHAVINGMETHOD_PREF_IMM16 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_DYNAMICIMPORT_PREF_V8 | ObjectAccess | 0 | none | Unmeasured |
| DEPRECATED_ASYNCGENERATORREJECT_PREF_V8_V8 | ObjectAccess | 0 | none | Unmeasured |

## Family impact (distinct records, not summed opcode hits)

| Family | Variants observed / total | Occurrences | Cohort records | Located first refusal previous / V4 |
|---|---:|---:|---:|---|
| Call | 24 / 31 | 82089 | 548 | 11 / 9 |
| ObjectAccess | 85 / 113 | 197500 | 572 | 19 / 6 |
| Creation | 24 / 26 | 21212 | 521 | 3 / 4 |
| RegisterLoad | 1 / 1 | 315705 | 572 | 8 / 4 |
| Throw | 12 / 12 | 24360 | 518 | 2 / 2 |
| Definition | 7 / 8 | 23814 | 569 | 2 / 2 |
| Constant | 16 / 18 | 36652 | 620 | 0 / 0 |
| Iterator | 11 / 12 | 10600 | 392 | 0 / 0 |
| Binary | 18 / 18 | 9438 | 327 | 0 / 0 |
| Unary | 10 / 16 | 2772 | 381 | 0 / 0 |
| Comparison | 4 / 4 | 5532 | 411 | 0 / 0 |
| RuntimeCall | 26 / 26 | 7434 | 383 | 1 / 0 |
| String | 1 / 1 | 20546 | 582 | 0 / 0 |
| Jump | 37 / 37 | 19018 | 499 | 0 / 0 |
| Move | 3 / 3 | 152241 | 620 | 0 / 0 |
| RegisterStore | 1 / 1 | 339347 | 572 | 0 / 0 |
| Immediate | 2 / 2 | 12456 | 514 | 0 / 0 |
| Return | 2 / 2 | 28970 | 620 | 0 / 0 |
| Nop | 1 / 1 | 13 | 0 | 0 / 0 |

## Current unsupported instruction first refusals (exploration)

| Flags | Input / record | Method / original PC | Decoded opcode |
|---|---|---|---|
| previous | o14/28934 | 29066@9 | CALLRUNTIME_DEFINESENDABLECLASS_PREF_IMM16_ID16_ID16_IMM16_V8 |
