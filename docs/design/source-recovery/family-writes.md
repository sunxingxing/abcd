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

## Cost and Publication Boundary

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
