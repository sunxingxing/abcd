# Exception Control Projection

This is the T09 control step of the common source pipeline. It is not an
alternative body writer, a source publication receipt, or completion of group 1.
The original phased `ValueProgram` and `ExceptionPlan` remain authoritative.

## Inputs And Output

`BuildExceptionControl` first verifies the original body, phase import, and
exception-region plan. It then constructs an explicit topology projection and
uses the existing acyclic postdominator/region discovery. The projection is a
`ControlTopology`, not a copied `ValueProgram` with its phases removed.

`ExceptionControl` owns a shared `ControlPlan` skeleton and typed coordinates:

- `PhaseControlUnit` refers to an original Before, Commit, or Catch block.
  Original blocks retain their index prefix; no phase is merged or erased.
- `TryControlUnit` refers to an original TryId. Its body and handler are separate
  skeleton roots; the wrapper's continuation is the common normal exit, if any.
- `TryExitUnit` terminates a protected/handler root normally. It records its
  original EdgeId, or the nested TryId whose wrapper continuation exits its
  parent. This is not automatically a JS `break` and carries no instruction PC.
- `ControlArc.edge` maps a projected edge to the original EdgeId. Absence is a
  synthetic wrapper continuation. Raising edges are never included among normal
  arcs: their dispatch, operation, and catch payload remain in `ExceptionPlan`.
- `inactive_commits` retains only empty, disconnected Commit nodes of explicit
  throw. They participate in the structural census, but must not become emitted
  statements or declarations. Arbitrary unreachable operations are refused.

Do not use skeleton node/edge indices as original IR IDs. In particular, do not
pass this skeleton into the normal function-tree builder. Its original phased
graph still fails normal control verification.

## Region And Exit Algorithm

Validated exception regions are disjoint or nested. Build one innermost lexical
owner per phase and DFS intervals for the region parent forest. A Catch phase
belongs to its region's handler, even when a nested protected region starts at
that same instruction. Before/Commit follow the instruction's innermost scope.

Inspect each normal edge once. If it leaves its innermost region, record that
region's normal exit target. Propagate child exit summaries once in reverse
outer-before-inner order. Multiple distinct exit targets are a typed refusal;
return/throw paths do not require normal continuations.

Index region entries by `(parent scope, original protected entry block)`.
Target resolution either stays in the same scope, enters the immediate nested
wrapper through this index, or terminates at a checked region-exit unit. This
avoids walking every ancestor for every edge and correctly handles nested
regions sharing a protected entry. An arbitrary jump into a region is refused.

Run the existing reverse-topological postdominator recurrence with binary-lifted
LCA on the projected forest. Existing normal CFG/natural-loop paths are not
replaced. Projected cycles currently return `kCycle`; future loop integration
must prove transfers across try boundaries instead of dropping backedges.

For I instructions, B phase blocks, E edges, and R regions, the adapter adds
O(B + E + R) storage and O(B + E + (E + R) log(R+1)) work. With U projected units
and A projected arcs, common discovery adds O(U log(U+1) + A) storage and
O((U + A) log(U+1)) work. Original import/region verification has its existing
separately documented costs. A shared quota charges validation, projection,
discovery, and skeleton checks; returned `allocated_bytes` counts owned results,
not peak process RSS. Inputs are caller-owned.

## Verification And Next Integration

`VerifyExceptionControl` rechecks the original phase/region evidence and typed
mapping. `VerifyControlSkeleton` independently verifies every projected edge,
branch arm, continuation, root, and unique node owner. It does not replay
postdominator discovery. Revision changes, missing arcs/roots, altered handler
coordinates, cycles, and duplicated nodes are rejected.

Tests use three historical profiles, decoded getter/call old-local cases,
protected diamonds with normal joins, shared-start protected nesting,
handler-start protected nesting, and 200/201/402 nested regions. Mutation tests
cover mapping, handler, exit, inactive commits, revision, edges, and node order.
Work-limit boundaries and zero-byte admission are exercised. Multiple normal
exits and region-local loops have explicit refusal tests.

## Shared Tree Draft

`BuildExceptionFunctionDraft` drives the existing function-tree builder and
printer with the original phased graph and typed projection. Catch parameters
are handler-scoped. Snapshot assignments precede throwing operations; catch
copies commit at handler entry; normal copies retain original edge identities.
Predicates move to Commit only across a checked empty Before-to-Commit boundary
without copies. `TryOrigin` identifies structural regions without invented PCs.

This is deliberately a draft, not a publication receipt. Decoded instruction
fixtures for three profiles execute generated JS and compare getter/call
effects, old-local snapshots, protected branches and nested catch/rethrow.
Expression origins include the original operation PC and printed ranges are
nonempty. This does not certify complete PC mapping or file-backed methods.

Still required before executable source publication (items 1-3 now have draft
construction, not independent semantic certification):

1. Let the common function-tree builder consume this typed projection, creating
   TryStmt and handler-only catch bindings, not function-body catch locals.
2. Place staged copies before their original throwing operations, catch commits
   at Catch entry, and normal copies using original EdgeId mappings.
3. Preserve Before/Commit ordering, including predicates whose branch successors
   belong to Commit. Never move normal result availability onto throwing paths.
4. Extend independent tree flow/operand/origin and ABI verification for those
   placements, then execute the same decoded function and check PC ranges.
5. Integrate region-local loops, more general exits/completions, catch captures,
   finally recognition and certified async owners. Current refusal is not final
   coverage and must not be recorded as completed ISA support.
