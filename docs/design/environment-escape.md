# Lexical Environment Escape

## Purpose and Limits

Identify fresh local environment allocation sites that cannot become externally
reachable under the modeled ordinary reference operations. This is a necessary
condition for later cell promotion, NOT proof of initialization, TDZ semantics,
last store, dynamic-instance equivalence or permission to erase an allocation.
Loop instances of one site remain distinct at runtime; analysis merges them
conservatively. No source coverage is claimed by this stage alone.

Inputs are one unchanged validated method/frame, its register SSA and ordinary
environment flow. Their normal-only, non-resuming, exception-free prerequisites
remain binding. Unmodeled scalar numeric representation transformations and raw
NaN constants fail closed: ordinary pointer analysis cannot justify a local
identity proof when tagged values might be manufactured from numeric payloads.
This is not an analysis of arbitrary native code fabricating VM references.

## Model

Create nodes for SSA values, local environments, one conservative captured
environment node, and actually referenced lexical bindings. Union copies,
Phi operands/results, allocation/current-environment results, and the values
stored into/read from each binding. This flow-insensitive merging may lose
precision but never treats different dynamic instances as one runtime object.

Directed containment edges connect child environments to parents and each local
environment to the value class of each referenced cell. Captured cells and the
captured environment are external roots. Non-lexical consumers of SSA values
are conservative escape roots, including calls, returns, throws, properties and
diagnostic reads. Function creation conservatively roots its current environment
(including sendable/unused creations; no target/capture shortcut is inferred).

Collapse aliases first, build CSR containment once, then propagate external
reachability once per class/edge. Do not union a container with its contents:
that would incorrectly expose a container just because it holds an external
value. Captured stores, aliases through lexical cells, and escaped children
holding parents are all retained in the reachability graph.

## Algorithm and Plan

Union by rank with iterative path compression follows the established
unification approach discussed by [Steensgaard, POPL 1996](https://www.microsoft.com/en-us/research/publication/points-to-analysis-in-almost-linear-time/).
This is a smaller, intraprocedural, environment-specific model, not an
implementation or precision claim for the full interprocedural paper. No
upstream implementation is copied. Inclusion-based points-to sets can offer
more precision but would introduce large repeated sets unnecessary here.

For V SSA values, P Phi inputs, I instructions, U register reads, L lexical
accesses, C referenced cells and A environment allocations, let N=V+C+A+1.
Indexed extraction and sorting cost O((I+L+V) log(2+I+L+V+P)); union/find work
costs O((V+P+I+L+U) alpha(N)); CSR and
reachability cost O(N+C+A). Memory is O(N+C+A+L), not declared slots times CFG.
Shared method work quota and counted PMR scratch include sorting comparisons,
union/find work, graph propagation, growth overlap and the owned result copy.
Caller-owned SSA/body/environment flow must be separately admitted.

Implementation sequence: failing lexical-alias escape test; typed owned API and
solver; extraction using existing typed lifts; adversarial containment/capture,
Phi/loop, lifetime, budget, 200/201/geometric tests and independent small oracle;
Debug/sanitizer/isolated RSS verification; independent review and commit.
