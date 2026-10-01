# Complete-Family Write Domains

`ClosureWrites::kLanguage` means transitive children write only JavaScript values.
It does not imply an unchanged value, constness, identity stability or a safe
snapshot. `VerifyFamilyWrites` discharges this domain obligation for the currently
supported complete file-family construction; it does not publish source.

## Evidence

The checker consumes unchanged original inputs and working graphs together with
fresh semantic trees, default-signature proofs, tree flows and capture layout.
These prerequisites remain binding; the checker does not rebuild a planner or
accept arbitrary caller-constructed trees as semantic evidence.

- The root has no incoming capture obligation. Every member has the original
  identity and the matching work/CFG/cell/flow revisions.
- Every original lexical store reads only Language values according to the
  immutable input SSA proof. Stores remain subject to operand validation; a
  conditional capture-read premise alone is not proof of store initialization.
- A retained store must map back to its exact original site/PC and forward to
  an AST assignment to its actual cell symbol. Counting all live residual stores
  rejects unaccounted extra stores. Per-function semantic/effect checks still
  establish value correspondence, sequence and snapshots.
- A removed original store must belong to a previously verified default-signature
  migration. Incoming/captured stores cannot use this exception.
- Each capture barrier has exactly one real child creation or an unchanged
  primitive-only `ReadObjectBuffer` result. Missing, duplicate and surplus evidence
  are refused. Methods/accessors in buffers remain unsupported by that reader.
- Each child is linked to an earlier occurrence owner; its original initialized
  capture map, residual cell rebase and actual parent/child AST references are
  rechecked. Distinct creation occurrences are not collapsed by method entity.

Together, initialized capture edges and all Language-only stores establish the
closed family's write-domain invariant by induction over execution steps. The
proof is parametric in legal Language inputs and the existing versioned host
contract. It never strengthens values to non-undefined or immutable, and does
not justify caching a cell read across a call. Environment handles must remain
unobservable under the existing cell-evaluation checks.

Normal-flow edges also admit deferred initialization in a fresh environment's
unexposed construction prefix. The original local analysis records each cell's
first store only while the same block contains allocation, effect-free scalar
assignments, function creations and stores into that exact environment. A load,
call, debugger, external store, branch or other operation ends this prefix; it
cannot restart without a fresh allocation. No closure can execute or escape
before its required store. Allocation failure cannot expose the environment;
exception-handler methods do not use this normal-flow proof.

This alternative requires Language-only transitive stores and retains Defined
and unknown-write rejection. It discharges future child reads without claiming
the cell is initialized at creation. Local reads retain their original strict
initialization checks; source emission preserves closure creation and store
order. The decoded deferred-capture fixture traverses file-family binding,
common AST, capture/write verification, preparation and printing; its runtime
test checks returned values and separate cell instances across calls.

## Cost and Publication Boundary

Phased file-family evaluation now forwards the retained `ExceptionLocals.writes`
premise into `BuildPhasedEvaluationPlan`, including primitive buffered objects
with no child `FunctionBinding`. The standalone API defaults to Unknown. The
plan and its cell-initialization witness retain the same typed premise, and the
independent evaluation checker rejects disagreement or erased premises. This
does not turn conditional Language writes into initialization, constness or a
snapshot: missing store-Commit dominance into catch remains rejected.

The actual file-family path continues through `VerifyFamilyWrites` before its
complete result is returned. Original buffer instructions require primitive
object coverage even with zero descendants; removing that coverage or replacing
the phased local premise with Unknown refuses the family. Serialized ABC fixtures
for profiles 12.0.2/12.0.6/13.0.1 exercise file reading, the shared tree, flow,
ABI, instruction sources, write checking and printing. Independent JS reference
tests retain callback/exception identity, undefined receiver, fresh own-property
objects, mutations and native non-callable TypeError behavior. These are scoped
functional checks, not full-corpus or platform acceptance.

This forwards an existing input contract and reuses the existing algorithms;
additional production cost is O(1), with no new dependency, index or proof wrapper.

Site-indexed masks make original-store/signature/barrier coverage linear in input
and work operations. Capture verification retains its existing indexed rebasing
cost, proportional to actual occurrence/access records rather than repeated
whole-parent scans. Scratch arrays, comparisons and verification work share the
family budget. No thread scheduler, source text matching or sidecar is involved.

`FamilyWriteFacts` exposes counts only and is explicitly not a transferable proof
or an executable receipt. External record/module/global bindings, strict source
enclosure and remaining callable/exception/async obligations remain separate.
Unsupported children cannot be skipped to obtain a successful parent result.

The current 7449 ABC observation has no captured store. A file-level nested-writer
fixture is specified in `tests/unit/source/creation_tree_context_test.cc`; it and
the CLI accounting checks remain uncompiled/unrun during the testing pause.
No JS execution, mutation-equivalence or full-corpus completion is claimed.
