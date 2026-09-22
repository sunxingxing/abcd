# Single-Block Loop Source

## Scope and Proof

Emit genuine JavaScript loops for normal, exception-free CFGs whose cycles
consist solely of self edges. All non-self edges must form a DAG. Retain the
existing acyclic API's rejection of every reachable cycle. A new block-loop
entry point supports the larger shape, and the source probe uses it.

Use Kahn ordering on non-self edges; still validate every edge and discover all
normally reachable blocks. This is contraction of singleton loop regions, not
a program-counter interpreter. A cycle involving multiple blocks remains a
typed cycle failure. No SCC expansion, duplication, or irreducible fallback.

For each self-loop block, emit `while (true) { ... }`. Original taken/fallthrough
conditions remain unchanged. A self edge emits `continue`; an exit emits a
break to the existing enclosing forward label. Instructions occur once in the
generated text and execute in original order on each iteration. Label targets
remain outside the loop. Budget both labels and the extra loop nesting.

Accumulator analysis uses the existing three-state lattice and language-value
register precondition. Join the first-entry state with the self-edge exit.
If a value entry is weakened by a Hole exit, replay that block once from the
weakened entry to check reads. Transfer functions are preserve or constant
writes, so this is a fixed point in at most two scans. Propagate only non-self
exits in quotient order. Do not assume the first iteration proves later ones.
This does not permit emitting Hole, recover wrappers, or prove VM numeric
representation equivalence beyond the existing scalar emission contract.

Compared with a general loop structurizer, this step is deliberately limited
but directly usable in source generation. General multi-block loops require
their own region/exit proof and remain an outstanding part of the full goal.

## Bounds and Implementation Plan

Kahn traversal and accumulator scans cost O(I+B+E); no recursion or per-loop
whole-graph walk. Existing graph/analysis/output memory and preflight contracts
remain in force. No new body-sized copy or generated-output buffering.

1. Add source-loop tests and a new API initially delegating to the acyclic
   writer; verify a valid self-loop fails that test.
2. Share ordering and accumulator code behind separate public acyclic and
   block-loop entry points. Test Hole-on-backedge and multi-block rejection.
3. Extend the private ordered writer with structured loop/edge emission and
   account nesting. Keep acyclic output byte-for-byte compatible.
4. Run generated source in Node against loop fixtures with side effects,
   exceptions, finite branches, and observable iteration counts. Test output
   preflight, limits and geometric scaling. Review the proof independently.
5. Run Debug/sanitizers and isolated source corpus probe under the 1 GB cap;
   preserve static classifications and commit evidence with the implementation.

Body readiness is not a recovered function wrapper or a complete record file.
