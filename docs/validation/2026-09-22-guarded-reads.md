# Guarded Read Refinement Validation

## Scope

Normal, exception-free SSA reads gain same-value dominating Hole-guard
evidence. Global value flow is unchanged. Remove only Hole; retain environment
and other internal categories. The guard's own reads precede its refinement.
No copy/Phi-path propagation, heap cell proof, source rewriting, guard removal,
async task service or new complete record source file is included.

Implementation and bounds: [design](../design/guarded-reads.md).

## Results

- Debug warnings-as-errors build succeeded; CTest **71/71**, 24.47 s.
- ASan + UBSan `guarded_reads_test`: **1/1**, 14.49 s.
- Isolated Debug run after other build/test processes completed:

  ```sh
  prlimit --as=1000000000 -- /usr/bin/time -v build/decode/guarded_reads_test
  ```

  Exit 0, elapsed 3.85 s, peak RSS **70,460 KiB = 72,151,040 bytes**.
  No child processes. This includes synthetic fixtures, CFG, SSA, value flow
  and refinement. It does **not** prove whole-engine/full-corpus peak memory.
- Independent review found no remaining concrete semantic, complexity,
  ownership or budget issue; a second pass reviewed the expanded oracle/tests.
- One worktree; no source input samples modified. No corpus scan/classification
  rerun and no change to previously reported source coverage.

## Tests

- All 15 nonempty category combinations: guard removes only Hole; Hole-only
  becomes bottom at a later read, never a language-value proof.
- Guard's own value/name reads, name-pool guard, diagnostic-name non-refinement,
  repeated guards, same-block assignment and copy non-propagation.
- Bypass diamond, nested guard rollback to a common ancestor, entry-loop Phi
  read before/after its guard and replacement input on the backedge.
- Owned output after all inputs die, move construction/assignment, invalid
  lookups, handler/resume/invalid partition and malformed guard failures.
- Every shorter work quota on a fixture fails; exact quota/peak budget passes;
  one byte below observed peak fails without publishing a partial result.
- 500 generated small cyclic CFGs: an independent instruction-level graph
  comes directly from decoded jumps. Removing each candidate guard node tests
  dominance, without calling the production dominator builder. Compare every
  reachable read's categories and validate the returned same-value evidence.
- Flat blocks and deep dominator chains of 200, 201, 4,096 and 65,536 guard/read
  groups. Validate every guard/read, instruction/guard counts and work bounds.

Pinned interpreter source supports the normal-continuation rule. No Ark runtime
execution or runtime differential claim is made by this component test.
