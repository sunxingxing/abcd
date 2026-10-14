# Round 66: family creation refusal evidence

Baseline: `bd4029c7`. This increment is diagnostic only. The evidence-first
investigation found no local admission fix among the five requested records,
so it stops at step 1 as instructed. No creation is skipped, reclassified as a
function, or admitted by deleting a class ownership check.

## Exact first checks

Both flag sets have the same five cases. The original `FamilyFileError::kCreation`
(value 1) remains first. The new fixed-size `FamilyCreationEvidence` carries the
check, the actual creating method/working Op/instruction, original and working
PC, family root/occurrence, and constructor/literal **pool slots scoped by the
creating method**. These slots are not method entities. A descendant's incoming
creation is recorded separately from its failing class creation.

| cohort/record | creator, Op, original/working PC | root / occurrence | incoming descendant source | constructor/literal pool | actual check and leaf |
|---|---|---|---|---|---|
| o18/52728 | m53236, Op67, 179/179 | 53180 / 2 | m53180 Op8 @21/21 | 1154 / 1161 | FunctionBinding (9) > CreationError::kUnsupportedOpcode (0) |
| o18/59564 | m60156, Op103, 232/232 | 60072 / 6 | m60072 Op12 @32/32 | 1328 / 1329 | FunctionBinding (9) > CreationError::kUnsupportedOpcode (0) |
| o18/60829 | m61449, Op48, 126/126 | 61365 / 9 | m61365 Op16 @43/43 | 1359 / 1364 | FunctionBinding (9) > CreationError::kUnsupportedOpcode (0) |
| o18/63861 | m64425, Op20, 46/46 | 64341 / 2 | m64341 Op8 @21/21 | 1437 / 1443 | FunctionBinding (9) > CreationError::kUnsupportedOpcode (0) |
| o36/19453 | m20296, Op8, 18/18 | 20296 / 0 | no incoming family edge | 250 / 268 | PhasedSite (8), absent from phased function-creation index |

`abc_semantic --instructions` resolves the constructor pools to methods 53264,
60212, 61505, 64453 and 20600 respectively. The diagnostic itself uses existing
pool provenance instead of performing another fallible file lookup.

All other `kCreation` checks are distinguished too: body/semantic site range,
root projection site/op/liveness/owner/kind, missing phased index, capture-flow
identity, child ordering, and self-initializer indexing. No enum value of
`FamilyFileError` or resource classification changes. The normal binding's
actual typed `CreationBindingResult` error is retained, rather than inferred
from the operation kind.

## Original CFG: m60156 is reachable

A temporary tool outside the repository decoded the **original** body with
`DecodeMethodBody` and ran `BuildNormalFlow`, not the transformed value graph.
There are no try regions. One entry-to-class path is:

`B0 -> B1 -> B3 -> B4 -> B5 -> B7 -> B9 -> B10 -> B11 -> B13 -> B15 -> B16 -> B17 -> B19 -> B21 -> B22 -> B24 -> B26`.

In particular, B19 [125,130) has a taken edge to B21 [174,185): the branch at
PC128 targets PC174 and bypasses the return at PC173. B24 [189,194) has a taken
edge to B26 [229,439): PC192 targets PC229 and bypasses the return at PC228.
B26 contains `ldhole` at PC229, `sta` at PC230 and the class creation at PC232.
The preceding return in file order is not a reachability proof. Raw decode,
original CFG and CLI receipts are retained under `/private/tmp/r66-evidence/`.

## Why no local fix

The four normal cases reach `FamilyReader::Read`'s `BindCreation` call.
`analysis::BindAtEnvironment` first uses `LiftFunctionCreation`, whose domain is
DEFINEFUNC/DEFINEMETHOD. Their class creations therefore return the actual
`CreationError::kUnsupportedOpcode`.

This is a missing class-family composition path, not a missing opcode semantic:

- `FamilyReader::Add` installs `ProjectClassMembers` only for a normal
  `FileFamily::kRoot` with `FamilyClassMode::kRetainRoot`.
- `FamilyTrees::Build` only accepts retained class obligations on the supplied
  record entry; its other family members must have no class obligations.
- `record_tree.cc` prepares and builds the class/member families and supplies
  the resulting `ClassTree` constants. `expression_forest.cc` requires these
  verified constants before creating a `ClassExpr`.
- `FamilyWrites::Classes` independently requires a normal root and verifies the
  composed bodies against each class barrier. Removing only the reader check
  cannot establish these write/capture correspondences.

The phased case additionally reaches `BindPhasedCreations`' index, which contains
only live `ValueFunctionCreation` operations. Its class creation is correctly
absent from that index. It needs a distinct class occurrence/preparation path,
not a fabricated phased function entry.

Suggested next design: prepare class requirements for **normal descendant
creators** before their declarations/trees freeze, with per-occurrence ownership
and the existing class initializer/self-binding/deferred-capture proofs; build
and bind their verified ClassTree constants; independently extend FamilyWrites
and recovery correspondence to those creators. The phased variant additionally
needs Before/Commit/catch and class-heritage scope correspondence. This spans
family discovery, preparation, emission and verification; it is outside this
round's local-fix condition. No new language premise is identified by the five
refusals; this is not a claim that all later obligations are already proven.

## Diagnostic boundary and validation

Failure witnesses use only O(1) borrowed lookups and fixed-size values, constructed
on failure. No new production buffer/index or successful-path planning state is
introduced. JSON uses the existing diagnostic boundary. Both route/manifest and
record-entry JSON serialize the same witness; receipts validate and round-trip
it; abc_semantic consumes the same serializer.

`family_creation_failure_test` covers all 13 checks, actual nested error enums,
PC 0 vs absence vs distinct working PC, descendant origins, invalid enum/ID/pool
records, resource-class preservation, both JSON paths, and 200/201/400/800
fixed-size serialization runs. These are diagnostic scale checks, **not** a new
recovery algorithm or a claimed exact recovery-budget proof.
`family_creation_failure_cli_test` checks the five real ABC refusals under both
contracts against the exporter, including instruction/pool identity and the
branches bypassing m60156's returns. No new es2abc/Node semantic differential is
claimed: no fixed semantic group or published source was added in this increment.

Full keep-going build passed; fixed regression 65/65; new tests 2/2;
family/creation/class/route diagnostics targeted tests 26/28 (only the documented
pre-existing family_source_cli_test and class_tree_source_test); CLI/corpus/
failure/receipt/manifest sweep 57/58 (only the former). The class_deferred loop
fixture's stale collapsed-leaf assertion was updated to the full PhasedSite
chain plus exact creation/descendant locations. Its refusal, absent output,
TDZ Node comparisons and mutations remain strict.

Both cohorts remain 513/620 and 552/620, with every status unchanged and every
already-complete/partial source byte-identical. Each cohort has exactly five
changed record objects, solely their source_failure fields (the table above).
No other manifest record fields changed. Cohort output: `/private/tmp/r66-cohort/`; logs:
`/private/tmp/r66-evidence/`. No paired source is used for diagnosis.
