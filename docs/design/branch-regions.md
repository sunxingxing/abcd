# Multi-Block Branch Regions

RegionBranch extends the common region tree with a header, two owned child
regions and their forward continuation. It is distinct from the existing
single-block RegionIf so conditional-value recovery retains its exact admission
and original-PC contracts. Both feed the same AST and source printer.

## Interval Proof

The complete reachable acyclic normal graph is already in validated forward
source order. For header position h, require fallthrough at h+1 and taken at a,
with a>h+1. Query the body interval [h+1,a): its earliest predecessor must be at
least h, its largest successor j must exceed a, and its second-largest distinct
successor must be below a. Thus all body edges stay inside the body or leave at
j; no edge enters the alternate interval. Query [a,j): its earliest predecessor
must be at least h and its largest successor must equal j. All alternate edges
therefore remain inside or leave at j. Forward order excludes later outside
entries; header's two edges are the only earlier permitted entries. Returns or
throws within either interval remain terminal source statements.

A standard iterative segment tree combines minimum predecessors and the two
largest distinct successors. This associative summary supports O(log B) range
queries without rescanning each candidate body. It is an in-tree range-query
implementation, not a dependency or a claim of general postdominator recovery.
The two-arm contiguous SESE admission is conservative: missing convergence,
shared entries, cross-arm edges and noncontiguous layouts retain labels.

## Construction And Emission

Recursive construction consumes only candidates whose join lies within the
parent interval. Header and each arm block are owned once; no effect or block
is cloned. Existing simple if/else reductions run first. Child sequences retain
their normal label structure, including explicit exits to the enclosing join.
The common printer emits the header predicate once and visits the two child
regions under native if/else. Each arm receives the proved join as its natural
continuation. Only a sequence's final child inherits it; labels pass it through,
while loops and try regions do not. A block's final unconditional jump or
fallthrough to that exact continuation needs no emitted break. Earlier exits
and different targets remain explicit. Label-use collection applies the same
rules, so it can omit a join label only when no emitted exit references it.
Elided final jumps keep zero-width PC anchors. Original instruction spans are
emitted directly; fused predicate origins follow the existing relocation rule.

Methods with a cross-block staged argument region conservatively print the
equivalent forward-label layout for RegionBranch. Label-use collection makes
the same decision, preserving that argument owner's independent emission and
capture scopes. Record lexical scope checks remain active. Loop and exception
builders do not create the new node; their admission is unchanged.

For B blocks and E edges, proof costs O(E+B log B), construction O(B), and
emission retains existing instruction/output costs. The admitted scratch is B
optional intervals plus 2B tree summaries; actual capacities are checked. Node
accounting is increased for the two child owners, and the original region depth
limit remains. No per-variable walk, effect reordering or dispatcher is added.

Tests, generated JS execution and RSS remain paused. Deferred regressions
include nested/multiple joins, outside entries, cross-arm exits, terminal arms,
parallel edges, non-power-of-two trees, root prefixes, argument-owner fallback,
scope-sensitive declarations, source maps, budgets and 200/201 scaling.

## Production Evidence

Both production CLIs built. ABC-only export
`build/pasteboard-branch-continuations-20260925` recovers native outer if/else
in the five Builder callbacks, including their existing inner conditional
values. The now-unreferenced outer join labels and final arm jumps disappear.
Home and the common modules remain unchanged. Output totals are 3,702 lines /
84,688 bytes, down 35 lines / 400 bytes from source-block-order.

PageA callback 7089 predicate PCs 17/20 map to [898,921), the emitted
`if (isInitialRender)` header; its body exit PC 120 retains the zero-width
anchor [1407,1407). Partial maps retain 195 methods / 9,745 spans. Statuses
remain eight incomplete records, two constructors refused at PC 14 and nine
metadata-only records. Read-only review of interval isolation and continuation
propagation found no concrete defect. No tests, JS execution or RSS tracking
ran; this does not prove full application equivalence or record completion.
