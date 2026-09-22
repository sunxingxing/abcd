# Guarded Register Reads

## Contract

Refine each reachable SSA read using a prior, normally completed Hole guard of
the **same SSA value** that dominates that read. The guard's own reads happen
before its successful continuation. Preserve the original global value facts;
remove only Hole, never environment/other internal categories. Empty remains
bottom, not a language-value proof or permission to delete effects.

Inputs are the same unchanged validated method, normal CFG, register SSA and
global value flow. Reject handlers/resumes and structural mismatches. This is
not exceptional dominance, cell alias analysis, copy equivalence, Phi-path
refinement, source emission, guard elimination or the function task service.
Guard evidence records an instruction ID, not a string semantic tag.

Pinned evidence: `ets_runtime/ecmascript/interpreter/interpreter-inl.cpp`
lines 4092-4125 implements both register/name-pool variants. The successful
branch dispatches before reading the diagnostic name; the failure branch raises
and goes to exception handling. Repository identity is in the 7.0 release lock.
Read facts preserve SSA's conservative may-read list, not unconditional name
evaluation. Boundary checks do not certify that independently supplied inputs
belong to the same method; their unchanged identity is a caller precondition.

## Algorithm and Alternatives

Reuse the existing iterative Lengauer-Tarjan dominator builder. Traverse its
preorder intervals, keeping the last active guard by SSA value. Record reads
before activating a guard. Undo only actual guard updates when leaving a
subtree. A later assignment has a distinct SSA ID and gains no old proof.
Sort owned read records by instruction/ordinal for logarithmic lookup.

A global fact update is unsound across bypass paths. Testing every guard
against every read is quadratic. Full predicate SSA would propagate more facts
through copies and joins but requires a separate proof and is not introduced
here. This deliberately conservative first stage has one graph traversal.

For B blocks, E edges, I reachable instructions, V SSA values and R reads:
O((B+E) log B + I log(2+R) + R log(2+R) + V) time,
O(B+E+V+R) additional space. Guards are a subset of instructions/reads.
The shared method quota covers traversal, updates, reads, sorting and copying.
The existing dominator builder is admitted once by block count and memory,
not interrupted at every internal step. Its peak and retained capacity count
alongside scratch and owned output. Caller-owned inputs remain caller budget.

## Implementation and Verification Plan

1. Add a failing unit test for a guarded Language|Hole entry register;
   distinguish the guard's own read from the later read.
2. Add typed `GuardedReads` with owned read facts/evidence and budget failures;
   use a private local scratch resource, no borrowed result storage.
3. Test bypass diamonds, loops, reassignment, register/name guards, bottom and
   unknown, copy non-propagation, invalid topology, lifetime and exact budgets.
4. Compare against a small independent path-removal dominance oracle; exercise
   200/201/4096/65536 instructions and deep dominator trees without recursion.
5. Run Debug and sanitizer tests, isolated RSS under a 1,000,000,000-byte
   address-space limit, independent review, then commit the verified increment.

No new complete method/record source coverage is claimed by this analysis.
