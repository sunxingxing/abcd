# Round 67: normal descendant class preparation, increment I

Baseline: `220cd6da`. Only discovery and common preparation coordination are
implemented. Descendant classes are **not published**. Requirements, actual
creator storage, declaration fulfilment, enclosure and barrier completion
remain increments II/III. The phased creator is unchanged.

## Shared coordinator

`creator_classes.{h,cc}` extracts record_tree's ordered class layout discovery,
member-family preparation, and final construction. The record entry still owns
its module enclosure and declaration policy. Its existing deferred-request
merge consumes the same prepared member inputs, before BuildFunctionTree.
The final tree is still independently checked after actual class bodies replace
the provisional layout constants. Printing and names are unchanged.

`DiscoverCreatorClasses` resolves class constructor/literal pools using the
actual creator, retains layout/member occurrences and indexes definitions and
initializer calls once. `VerifyCreatorClasses` rebuilds those identities from
immutable input, checks occurrence coverage and rejects duplicate operations.
A layout is not a body, capture proof or completed ClassTree. The task collection
also stamps the family root, first owner, creator occurrence, actual creator
method, input identity, value revision and CFG revision; the verifier rejects
a collection substituted from another occurrence.

The existing explicit FamilyReader DFS owns normal creator discovery, including
normal creators in a class member family. At a real class barrier it installs
the existing ClassProjection and retains the actual FamilyClassObligation.
It uses the shared discovery and verification component, then emits
`FamilyCreationCheck::kClassPreparationGate` (14), carrying the creator method,
original/working PC, operation, family occurrence, pool slots and descendant
source. Input failures use `kClassPreparationInput` (13). Existing enum values
and resource classifications remain unchanged. The gate is not an allow flag.
No member input is recursively prepared on this new discovery path.

## Owner and lifetime boundaries

A discovered task has no owner range. The direct constructor/member inventory
cannot stand in for its full ordinary-function-family extent. In the existing
record-root path PrepareCreatorClasses stores an exact half-open owner range
only after each family has been read. VerifyCreatorClassOwners re-counts those
families and checks consecutive, disjoint ranges; FinishCreatorClasses must
consume exactly that range. No symbol or source name is allocated during the
new descendant proof.

Definitions and layouts are owned by the preparation object. References to the
unchanged creator and initializers are borrowed only for its synchronous use;
member vectors may grow only outside such a borrow. Allocation buffers are
charged before reserve/resize; allocate_shared uses a rebinding charged
allocator so its actual control-block allocation is charged, not a guessed
multiplier. Work is bounded, indexes use sorting/binary lookup, and traversal
is iterative: O(N log N) in actual instructions/edges/occurrences/names. This
increment does not claim a completed recursive class-family scheduler.

## Acceptance and remaining work

The four Socket records must retain status 3 and reach the new gate:
52728 m53236 Op67 @179; 59564 m60156 Op103 @232;
60829 m61449 Op48 @126; 63861 m64425 Op20 @46.
The original CFG proves m60156's class reachable (Round 66); no instruction is
removed because it follows a return in physical order.

Unit tests use real es2abc width/depth fixtures at 200/201/400/800, exact budget
boundaries and pool/creator/occurrence/range mutations. The existing family
failure JSON test covers every new reason on both serialization paths; the
CLI test compares manifest and abc_semantic for both contracts, including the
unchanged phased case. No executable descendant class or Node oracle is
claimed before joint tree/write/enclosure verification is implemented.

Next: collect class-family requirements before the creator's cell plan and
symbols settle; use the real CaptureStorage view, not raw FileFunction cells;
include nested free names and full owner extents; finish class bodies under the
actual creator and independently discharge each original barrier. Keep guarded
vs source-access vs strict demands, activation, self/outer bindings, static
initializer timing, and phased-parent boundaries unchanged.

## Workspace validation (2026-10-14)

The final workspace comparison against explore66/explore66s keeps 513/620 and
552/620. Every status and existing complete/partial source byte is unchanged,
including o32/49868. A full manifest comparison has exactly eight changes: the
four target records' `source_failure` under each contract. All other fields
are unchanged. Evidence: `/private/tmp/r67-final-cohort/`, including
`manifest-changes.json`; command log `/private/tmp/r67-final-cohort.log`.

The width/depth tests exercise the actual FamilyReader stack, in addition to
the shared component. For each fixture, binary search finds the exact budget
that reaches the gate; one byte/work less must return the matching typed
resource class. Discovery verification also rejects simultaneous removal from
both the task list and the class summary by recounting live class operations.
Measured byte/work boundaries are in `/private/tmp/r67-final-scale.log`.
Depth includes compiler-generated qualified names; total input bytes, not
lexical depth alone, are the complexity dimension.

Final validation commands (all tests serial):

| Command / selection | Result |
|---|---|
| `cmake --build build/regression -j 6 -- -k` | pass |
| `ctest --preset source-regression -j 1` | 65/65 |
| `creator_classes_test`, `creator_classes_cli_test`, `family_creation_failure_test`, `family_creation_failure_cli_test` | 4/4 |
| `class\|capture\|deferred\|guarded\|family\|creation\|recovery`, excluding the four tests above | 65/79 |
| `cli\|corpus\|failure\|_receipt\|manifest` | 58/59 |

The 14 targeted failures are registered pre-existing failures in status.md:
family_source_cli_test, class_tree_source_test,
unguarded_derived_cells_source_test, unguarded_literal_closure_source_test,
unguarded_derived_literal_receiver_source_test, captured_receiver_source_test,
unguarded_captured_receiver_source_test, unguarded_forwarded_receiver_source_test,
reversed_captured_receiver_source_test,
unguarded_reversed_captured_receiver_source_test,
unguarded_reversed_forwarded_receiver_source_test,
reversed_initialization_captured_receiver_source_test,
unguarded_reversed_initialization_captured_receiver_source_test,
unguarded_reversed_initialization_forwarded_receiver_source_test.
The sweep's sole failure is family_source_cli_test. Their assertions were not
changed. family_creation_failure.mjs now expects gate 14 for the four targets,
retaining exact creator/pool/descendant evidence; the phased fifth case is
unchanged. Logs: `/private/tmp/r67-accept-{build,new,fixed,targeted,sweep}.log`;
commands/results: `/private/tmp/r67-acceptance.json`.
No holdout, full suite or RSS benchmark was run. Changes remain uncommitted.
