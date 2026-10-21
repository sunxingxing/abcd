# ISA occurrence oracles and indexed property access (2026-10-21)

This round starts at a964be2a:148 Unmeasured,156 SupportedUnverified,
0 SupportedVerified,28 NonExecutable. These are variant display counts;
profile-specific observations remain separate. No Ark runtime execution is
claimed. The programme is not finished by adding oracle columns.

The test-only transaction writer is called after existing differential
assertions and retains the input ABC, engine output, evaluation-only original,
manifest, cases, Node version and SHA-256 identities. Multi-record tests name
only records actually compared. The matrix reruns registered CTests, rejects
skips/failures/stale artifacts, and streams occurrence evidence into a disk
index. A Node pass means that record was differentially tested, not that every
instruction or branch executed. The importer independently checks current raw
lift and complete recovery for each fingerprint/profile/record/method/PC.

The separate round-trip column requires unique corresponding method identities
and identical ordered semantic families, including typed nested expression and
operator kinds. Branch and handler destinations must correspond by instruction
ordinal too. SDK instruction widths/PC offsets may differ; mismatches remain
recorded, not normalized away or counted as passes. The strict grouping tool
remains unchanged. Its whole-bytecode comparison is stronger than this
family/event oracle. Per-profile verification never transfers to another
profile; the332-row display reports which profiles actually verified.

## Production increment delivered

`LDOBJBYINDEX` imm8/imm16/wide and deprecated register-base encoding now become
ordinary typed PropertyRead; `STOBJBYINDEX` imm8/imm16/wide become strict typed
ValueWrite with an exact numeric key. No new source-expression family, opcode
name dispatch or fixture-dependent admission is added. Indexed load decoding
reuses the existing adapter, including its frame/width checks. Float64 holds
every uint32 index exactly; UINT32_MAX is preserved as a named property.

Pinned semantics:

- ets_runtime/ecmascript/interpreter/interpreter-inl.cpp:5718–5809: ordinary
  GetPropertyByIndex/LdObjByIndex; accumulator commits only on success. The
  deprecated form reads its encoded receiver register, not the accumulator.
- The same file:5811–5896: SetPropertyByIndex/StObjByIndex; accumulator preserved.
- ets_runtime/ecmascript/stubs/runtime_stubs-inl.h:745 and782: ordinary property
  Get and `SetProperty(..., true)` respectively. Own-store descriptor semantics
  remain on the separate IndexedWrite path; inherited setters/proxy Set are
  observable and must not be projected as object-literal definitions.

Only the existing property/Get/Set and recovery verifiers authorize source.
New lifting is O(1) per instruction, allocation-free. Existing graph/source
budgets still bound whole-method work. Exact semantic and builder budgets and
200/201/400/800 scales are tested; malformed widths/kinds/frame registers,
getter/setter exceptions and wrong-index mutations remain checked.

SDK default and O0 sources emit by-value operations for the tested numeric
lookups; no claim is made that the compiler emitted the distinct index forms.
Those encodings use valid constructed programmes in12.0.6 and24 profiles,
independently decoded by SDK ark_disasm and production abc_decode, recovered
completely and compared with JS oracles.

## Explicit remaining scope

The29 lift-ok variants are not all promoted by this increment. The regenerated
matrix and change list distinguish observed fixture coverage from still-missing
complete/oracle occurrences. This round does not implement implicit-this/super
stores, regex/BigInt/template literals, object spread/rest/prototype semantics,
or the remaining throw families. They need their actual typed value/reference,
site-cache or source-pattern projections; no unsupported instruction is renamed
into a superficially similar JS operation.

`LDNEWTARGET` cannot currently be treated as uniformly native `new.target`:
interpreter-inl.cpp:7047 is fatal; interpreter_assembly.cpp:7799 dispatches without
setting acc; compiler/interpreter_stub.cpp:6164 dispatches NOP; AOT
bytecode_circuit_builder.cpp:903 produces CommonArgIdx::NEW_TARGET. Therefore
neither a universal source form nor all-path NonExecutable classification is
justified. Existing typed refusal is kept. This path difference is separate
from the ordinary `new.target` construct, which es2abc may lower through its ABI.

Evidence, final counts and remaining variants are recorded in status.md and
build/review/isa-r1/. Large generated JSON stays under build/.

## Acceptance and exact remaining work

The regenerated ledger (`build/review/isa-r1/matrix.json`,844 input files) is
**137 Unmeasured /72 SupportedUnverified /95 SupportedVerified /28 NonExecutable**.
No variant moved backward.84 previously SupportedUnverified variants now have
real imported oracles. Eleven previously Unmeasured variants now verify:

- XOR2_IMM8_V8; EXP_IMM8_V8.
- LDOBJBYINDEX_IMM8_IMM16; LDOBJBYINDEX_IMM16_IMM16;
  WIDE_LDOBJBYINDEX_PREF_IMM32; DEPRECATED_LDOBJBYINDEX_PREF_V8_IMM32.
- STOBJBYINDEX_IMM8_V8_IMM16; STOBJBYINDEX_IMM16_V8_IMM16;
  WIDE_STOBJBYINDEX_PREF_V8_IMM32.
- WIDE_NEWOBJRANGE_PREF_IMM16_V8; WIDE_SUPERCALLTHISRANGE_PREF_IMM16_V8.

All95 verified variant display rows have actual profile24 evidence;12 also have
12.0.6 evidence. Profiles9/11.0.2/12.0.2/13.0.1 receive no oracle transfer.
The complete95-entry before/after list, with explicit profile IDs, is
`build/review/isa-r1/class-changes.json`; the29-target remainder is in
`lift-ok-targets.json`. Only4 of those29 now verify;25 remain Unmeasured.

Eleven registered producer suites publish45 transactions (11,040 occurrence
receipts before duplicate input joins). The ledger joins10,766 Node occurrence
observations and3,681 separate round-trip event observations; Ark runtime stays
null. This does not claim all branches executed.111 conservative method-level
round-trip mismatches are retained rather than classified as passes.

New fixtures:20 complete exports,274 differential cases,14 observed wrong-index
mutations, SDK decode plus abc_decode, and200/201/400/800 exact builder/semantic
budgets. grouping_bytecode.py reports **18 exact /2 different /0 unverified** at
both default and O0, after the existing real-lexer identifier-valued string
export-alias adaptation is applied independently to both sources. No instruction,
register, branch or handler is normalized. The two differing exports are the
two WideArray compiler modes: original `super(...);` becomes the established
recovery `return super(...);`. Both return the initialized receiver, but es2abc
emits an extra undefined-return test, register moves and one IC slot for its
general derived-return lowering. Diff files retain every change. Their Node
values/prototype/length agree; these are **not exact-bytecode passes**, and the
corresponding failed event matches do not grant round-trip oracles.

The first unadapted comparison also records4 SDK string-export compilation
failures, separately from instruction differences. The final comparison uses
the logged lexer adaptation and compiles all20 on both sides. The grouping-only
token checker correctly rejects all20 original-vs-recovered comparisons: function
forms, source binding spellings and export representation differ. This is not
a printer-only round, so those token failures are not used as semantic evidence.
All20 emitted files pass node --check.

Fresh old/new40-input cohorts against a964be2a remain605/620 source and542/620
previous flags: no new completions, regressions, or text changes. Full build;
source regression65/65; all scale11/11 including nested_members800; ISA11/11;
new targeted CTests4/4 plus the3-test/20-fixture Python suite. The touched sweep
passes211/216; only the documented family_source_cli, tree_structure, try_tree,
class_tree_source and try_tree_source failures remain. No printer expectation
was changed. Scratch logs are in `build/review/isa-r1-development/`.

A wide-rest source probe remains SyntaxResolutionError::kParameters at the
257-formal COPYRESTARGS method; it is not certified by a discarded fixture.
NEWOBJAPPLY's hidden-array extraction must not inherit the approved narrow APPLY
CallSpread premise without a separate proof or user decision. Remaining this/
super property, literal/site-cache, object-rest/spread and intrinsic-throw
families remain implementation work, with their original typed refusals.
