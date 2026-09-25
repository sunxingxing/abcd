# Implementation Progress

Development is performed directly by Codex. The external Claude controller
was stopped at the user's request; it must not be restarted for this plan.
Scope remains T00 through T12 in the dependency order from the implementation
plan. No task scheduler, additional host assumptions or sidecar source inputs.

## T00

Implemented route admission, identity-bound receipts, central validation,
checked serialization, parsing and obligation closure. Evidence rules are now
version 2; schema remains 2 and working revisions are explicitly absent.
Invalid receipts cannot be serialized as successful evidence. CLI publication
handles typed serialization errors. Holdout hashes exclude all development
path aliases; G1/G2/G3 independent candidates are explicitly unfrozen until
method-level inspection establishes their capabilities.

Regression specifications cover CLI option ordering, receipt contradictions,
missing fields, numerical narrowing, direct body/template/entry/record guards,
and a legacy-success versus required-route-refusal function. These tests have
NOT been compiled or executed under the current test pause.

Production build evidence after `e23d304`:

```text
cmake --build build --target abc_structure abc_semantic -j2
exit 0, both tools built
```

ABC-only inspection on 2026-09-26, without paired source:

```text
build/abc_semantic /home/rabbit/sample/abc_with_code/pasteboard/modules.abc 7586 --value-source
exit 1: route=value_tree, stage=decoded, coverage=unsupported
reason=route_unavailable, legacy_writer_used=false, evidence_rules=2
file_size=112024, record=6897, work_revision=null
```

This is expected admission refusal, not recovered source. Behavioral regression
and resource validation remain unverified. Existing review findings have been
addressed in implementation/specifications; this does not assert tests passed.

## T01 In Progress

Normal operand mapping, owned work graph, SSA import, use-chain checking and
independent input correspondence traversal are implemented. The method CLI now
calls BuildValueStage instead of refusing every decoded method at admission.
It builds fresh input analysis/proof without legacy AST/printer participation.
Receipt schema/evidence rules 3 require a working revision from ValuesImported
onward; pre-import and legacy products keep typed absence.

ABC-only inspection of pasteboard method 7586 now reports:

```text
stage=values_imported, coverage=incomplete, work_revision=0
operations=21, values=29, uses=23, blocks=3, edges=3, exceptional_exits=3
legacy_writer_used=false, failure=null, source_complete=false, executable=false
exit 1 (no complete source yet)
```

This supersedes its T00-only route-unavailable observation above. Independent
application/profile candidates, entry/phi adversarial cases and stronger
input/owner verification remain outstanding; no full T01 validation claim.
Regression specifications and generated-JS/resource execution remain paused.

Independent application/profile observation: Immersive method 21082, record
19072, hash `8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a`,
profile 13.0.1, reached ValuesImported through the same CLI. Its decoded body
contains named reads at PC14/36, a branch at PC24 and named write at PC40.
The graph has 21 operations, 29 values, 23 uses, 3 blocks/edges/exceptional
exits. Candidate 12473 was inspected but has no branch or member effects and
is not counted as G1 evidence. No paired source or holdout inputs were read.

Input correspondence now checks entry definition kind and formal ordinal
against FrameLayout, not merely the value domain. An entry-loop fixture keeps
both MethodEntryKey and its real backedge input; this specification is unrun.

### Remaining Input Work

Use immutable SemanticMethod/SSA and its source proof as the input layer.
Import a separate owned ValueProgram with typed operation operands, explicit
read occurrences and source correspondence. Retain entry-phi inputs and
per-operation exceptional exits. Reject handler/resume inputs at the normal
import boundary. Do not invoke the legacy SourceAst or a legacy printer to
fill missing stages. Admission progress must produce truthful intermediate
receipts, not complete-source claims. T02 through T12 remain outstanding.

### T02 Candidate Edits Started

Internal ReplaceUse and ErasePureOp now maintain bidirectional occurrence lists
without scanning unrelated users. Erasure retains stable IDs and marks origins
ElidedPure. The structural verifier understands removed occurrences and rejects
live users of removed definitions. Observable/throwing operations and control or
owner operations cannot be erased by this primitive. Regression specifications
cover multi-use relinking, removal and effect retention; execution remains paused.

The internal SimplifyValues batch now admits original capacities, one candidate
copy and all simultaneous scratch. It resolves identity-copy chains with a
linear functional-graph walk/path compression and replaces each occurrence once.
Reverse liveness starts at observable/control/owner roots and includes protocol
operands and phi dependencies. Dead phi cycles are detached as a batch, avoiding
erase-to-fixpoint scans. A failed edit, budget, revision or structural check leaves
the original untouched; a successful nonempty batch increments IR revision once
without changing CFG revision. A no-op batch preserves the revision.

The pure-op allowlist is exhaustive over the current operation variant; even a
member read with incorrectly empty effects cannot become erasable. Specifications
cover copy lengths 200/201/4096, multiple uses, stale/overflow revisions, budget
rollback, copy cycles, unused getters and dead phi cycles. They remain unrun.
The production abc_semantic target builds with these changes.

The independent transformed effect/input correspondence checker and source-stage
integration are described below. Complete source emission remains outstanding.

### Working Flow Verification

Normal import and both sides of a simplification batch now run VerifyValueFlow.
It checks stable operation placement, complete block/edge ranges, terminal
control shape, dominance and within-block definition order. A normal result is
available after the producing operation, never before it or after an always-
throwing completion. Each phi must have exactly one input for each incoming edge,
plus MethodEntry for block zero. Edge IDs preserve parallel branch alternatives;
an entry input must be an actual entry definition, not a loop-body value.

The verifier reuses the existing iterative simple Lengauer-Tarjan dominator
implementation with a remaining-work allowance. Sparse phi keys are sorted to
check uniqueness and combined with predecessor counts to prove completeness:
O(I+V+U+(B+E) log B+P log P), no value-by-block or phi-by-edge matrix. Additional
storage is admitted alongside the graph; dominance builder peak storage is
conservatively charged even after its scratch is freed. This is not RSS evidence.

Unreachable blocks and resume flow are explicit unsupported normal-import cases,
not silently discarded instructions. Rules revision 4 records the stronger
ValuesImported boundary; receipt schema stays 3. Unit specifications include
parallel edges, duplicate/missing phi inputs, unavailable values, malformed
terminators, unreachable blocks and exact dominator work limits. They are unrun.

Production abc_semantic build passed. Fresh ABC-only inspections of pasteboard
7586 and Immersive 21082 both passed the rules-4 import gate with the same
21 operations / 29 values / 23 uses / 3 blocks / 3 edges / 3 exceptional exits.
Both reported ValuesImported, revision 0, no failure, no legacy writer and
incomplete/non-executable output (exit 1). No test executable, generated JS,
holdout sample, sidecar source or RSS measurement was run.

### Phi Dependency Components

SimplifyValues now runs a single iterative Tarjan traversal of live phi
dependencies after identity-copy canonicalization. Components finish in dependency
order. A component collapses only when all external inputs resolve to one value
whose nonempty domain is no broader than each replaced phi's domain. Components
with no external input or distinct external values stay intact. One final
occurrence pass updates users, followed by reverse liveness and normal-flow
verification inside the existing rollback/revision transaction.

The additional algorithm is O(V+U) with explicitly admitted arrays and stacks;
no recursion, per-variable CFG walks, repeated input rescans or dense relation
matrix. References remain those in algorithms.md; this is a local implementation,
not a new third-party dependency. Tests specify chains and rings of 200/201/4096
phis, mixed external inputs, domain refinement, and the unanchored/self-only
case. The full batch rejects malformed entry flow before SCC processing. Test
execution and RSS measurement remain paused; no ValuesVerified/source claim yet.

### Verified Rewrite Boundary

VerifyValueRewrite independently checks the proposed representative map instead
of rerunning copy/SCC optimization. Only pure identity copies and phi equations
may substitute a value. All equations preserve or refine domains, and a reverse
use-list traversal must ground every alias in a fixed representative. Equations
without an anchor are rejected. Graph layout, operation payloads, input read
identities, incoming-edge keys, observable effects, exceptional exits and source
PCs must remain unchanged. Only proved pure deletion may mark an origin ElidedPure;
old tombstones cannot be resurrected or silently rewritten.

This O(V+U+I+E+origins) checker runs inside the admitted transaction, before revision
commit. It complements, rather than replaces, both normal-flow checks and the
immutable input correspondence verifier. The public analysis/value_simplify.h
boundary requires verified input identity/effects. The source wrapper establishes
those premises from a fresh method/proof before calling it. Rules 5 expose the
result as ValuesVerified with its committed revision, never as complete source.
CLI counts now distinguish retained operations/values/uses from stable-ID arena
slots. The structural source tree, binding/control plans and printer are still
required before publishing executable source. Test and RSS execution remain paused.

Fresh production-build ABC-only observations: pasteboard 7586 and Immersive 21082
both reached ValuesVerified under rules 5, revision 1, without legacy output or
failure. Each retains 7 operations, 9 operand occurrences and 15 value entries
from stable arenas of 21/23/29; all 3 original exceptional exits remain. They
still report incomplete/non-executable (exit 1). This is real pipeline evidence,
not execution of the regression suites or proof of full-corpus coverage.
