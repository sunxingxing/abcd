# Structured Argument Branches

## Contract

ProveArgumentIf refines the existing SourceArgumentRegion certificate. That
certificate already establishes a finite forward interval, no external entry
into its middle, and convergence at the call. The refinement requires exactly
four contiguous CFG blocks: header, fallthrough arm, taken arm, join. Header
edges must have those exact typed targets/kinds. Each arm must have one normal
jump/fallthrough edge to the join, not a coroutine resume edge. Neither arm can
enter the other or escape. This is ordinary single-entry diamond reduction,
consistent with the existing analysis region-if algorithm, without a new
dependency or licensing obligation.

The result is the shared analysis::RegionIf, not a special source fragment.
The enclosing region tree can choose a different topological arm order, so
searching that tree for a pre-existing diamond is insufficient. The refinement
uses the CFG and existing interval proof directly, in constant time with no
dynamic allocation. SourcePrinter retains one optional RegionIf per method.
Other shapes retain the original argument-region label writer.

## Emission

ArgumentRegion invokes the normal IfRegion/IfArm printer in argument mode.
The header begins after the already-emitted property read; the join ends before
the already-owned call. Statements receive argument mode so the outer region's
skip logic does not suppress these instructions. Both arms retain their full
assignments/effects, while the original terminal jumps become zero-width PC
anchors. The branch predicate uses the same direction and fused-origin handling
as normal if emission. Physical instruction order and span count remain stable.

The outer zero-parameter arrow IIFE remains: the property Reference is obtained
before argument effects, with lexical this/formals unchanged. This change does
not introduce a ternary expression, eliminate branch stores, duplicate getters,
or restructure the outer method's labels. Scope depth remains conservative and
all existing expression/capture/owner admission checks still apply.

## Evidence and Remaining Work

Production abc_semantic/abc_structure builds succeeded. The initial tree-search
export `build/pasteboard-argument-if-01` was unchanged, motivating the CFG
refinement. `build/pasteboard-argument-if-02` recovers 11 native if/else regions
across all six pages, replacing 77 lines of label/jump scaffolding. Total source
size is 6,135 lines / 115,077 bytes versus 6,212 / 115,957 previously.

Eight records remain incomplete, two host constructors refuse at PC 14, and
nine records are metadata-only. Partial maps retain 195 methods / 9,745 spans.
For Home method 6016, PCs 105/108 map to the native if header, PC 124 retains
a zero-width arm-jump anchor, and PC 135 covers the full width call. Positions
come from the printer, not generated-text reconstruction.

Independent read-only review found no concrete issue in the revised proof.
Tests, JS execution and RSS remain paused. Deferred regressions include reversed
edge orientation, arm cross-entry, non-join exits, resume edges, throwing branch
effects, nested staged calls, header property slicing, join argument slicing,
predicate-origin rebasing, zero-width jump anchors and resource limits. Full
source equivalence and process-memory compliance remain unverified. Ternary
recovery, redundant outer labels and general multi-block argument trees remain
future work rather than being represented as complete.
