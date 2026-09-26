# Compact Natural-Loop Discovery

This refines the approved T05 normal-loop stage. It does not authorize source
publication without the subsequent control-edge and executable AST checks.

## Choice and Provenance

The existing bytecode `FindLoopIntervals` is not reused for working IR: its
fallthrough adjacency and numbered backward-interval prerequisites need not
survive graph transformations or block reordering. Nor do we enumerate a full
reverse predecessor closure for every header and store inclusive membership;
deeply nested loops would make that representation quadratic.

For reducible normal flow, use dominator-driven reverse discovery with subloop
contraction, the algorithmic approach documented by LLVM LoopInfo. Consulted
primary reference: locally installed LLVM 18.1.3,
`/usr/include/llvm-18/llvm/Support/GenericLoopInfoImpl.h`, sections
`discoverAndMapSubloop` and `LoopInfoBase::analyze`.
Reference SHA-256:
`708643f735c0cb13706717cc828f7bf5a5f8c4417b0a5ac4868bb4c61e9fa0d5`.
That reference is licensed Apache-2.0 WITH LLVM-exception. This implementation
is independently written for the project's strong IDs and budgets; no LLVM
source/template is vendored or linked and no new runtime dependency is added.

Unlike the reference's inclusive block vectors and outer-loop parent walks,
we retain only each block's innermost owner and each loop's immediate parent.
Union by rank with path compression tracks the currently contracted outer loop;
a separate semantic label prevents the union representative from becoming an
accidental header identity. Existing simple Lengauer-Tarjan dominance is reused.

This is a narrower *analysis domain*, not a reduced final goal: T05's structured
source admits natural/reducible loops, and explicitly refuses irreducible flow.
The initial algorithm catalogue's Ramalingam/Havlak family remains a possible
extension if non-natural loop-region recovery becomes necessary. It is not
required merely to identify and reject a residual irreducible cycle.

## Data and Invariants

- Input is the original working normal CFG. It must have one reachable entry,
  bounded successor counts, consistent edge ranges and no resume edges.
  Operand/effect/exception correctness is a separate Value IR obligation.
- `NaturalLoop` stores a header and optional parent. Discovery IDs need not
  match block/PC order. Parents are discovered after descendants.
- `owners[WorkBlockId]` is the innermost owner, not all transitive memberships.
- `backedges[EdgeId]` identifies exactly the loop headed by that edge's target;
  two alternatives with identical endpoints retain distinct original IDs.
- `forward_order` is a topological order after removing dominance backedges.
  No edge is removed from the original graph and no phi key is changed.
- Dominator preorder stamps give a linear-space reverse order in which every
  descendant header precedes its ancestors. Physical block ordering is ignored.
- An unowned predecessor is claimed once. An already owned predecessor is
  resolved through its contracted set; a new subloop is attached once, and
  traversal resumes at that subloop header's predecessors. Never expand its
  inclusive body again.
- A claimed block must be dominated by the active header. Once all headers are
  processed, removing all dominance backedges must yield a DAG. Remaining
  cycles are typed `kIrreducible`, not invented source loop owners.

## Cost and Resources

Let B be blocks, E normal edges and L natural headers (L <= B). Predecessor CSR,
dominator ordering, owner/parent arrays and the residual topological pass are
O(B+E). Each edge seeds at most one initial backedge visit and one predecessor
expansion, so a work stack reserved for 2E entries bounds pending pushes. Each
block is claimed once and each child loop is contracted once. Set operations
have union-by-rank/path-compression cost O((B+E) alpha(B)); existing dominance
has its documented O((B+E) log B) bound. Storage is O(B+E), including results.

Every array capacity, traversal and set-parent walk is budgeted. The dominance
builder receives only the remaining grant; its peak scratch charge is retained
conservatively while discovery arrays live. No recursion, per-loop whole-graph
scan, hash-map expected-time assumption or nested inclusive membership appears
in production. Tests may use a separately bounded three-block slow oracle.
Performance/RSS measurements remain paused, so asymptotic reasoning is not a
claim of measured scaling or process-wide 1G compliance.

## Integration and Remaining Gate

The current acyclic ControlPlan path invokes this discovery when topological
ordering encounters a cycle. It now distinguishes natural-loop syntax pending
(`kCycle`) from irreducible CFG (`kIrreducible`). It does **not** yet emit loops or
declare the candidate forest to be an independently verified source certificate.

Next, control regions must consume this compact forest, assign every normal edge
to a unique continuation/loop-back/exit, and receive independent checks against
the unchanged CFG. Source Loop/Break/Continue ownership, backedge copy placement,
and MethodEntry's separate once-only initialization phase remain mandatory.
No blanket relaxation of the existing control rejection is permitted.

Unrun regression sources include self-loops, multiple/parallel backedges, nested
headers, non-layout-order blocks, unreachable and irreducible graphs, exact work
limits, and 1/2/200/201/4096 nesting depths. A slow oracle enumerates all 4096
three-block graphs with up to two successors and derives dominance by vertex
removal, independent of production discovery. Tests are not yet compiled/run.
