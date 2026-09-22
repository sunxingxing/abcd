# Register SSA Validation

## Implemented Boundary

`RegisterSsa::Build` composes semantic register def/use indexing, live-in,
pruned IDF and iterative dominator-based renaming. Published read/write sites,
typed value origins and edge-specific Phi operands are owned, sorted arrays.
Entry values remain unknown VM values. A Phi at the CFG entry includes an
explicit method-entry operand as well as real backedges. Duplicate source
edges retain their successor ordinals.

The naming pass uses a referenced-slot current-value vector and actual-write
undo log, not a per-block copy of the frame. Existing cross-variable live-in/
IDF costs remain output-sensitive and can repeat work; a mandatory shared quota
is retained across every query. This does not solve that asymptotic limitation.
Nested stages receive only the remaining byte budget, with retained payloads,
temporary peaks, vector growth overlap and final output copies accounted for.

## Verification

- The initial target failed because `register_ssa.h` did not exist; the decoded
  diamond passes after implementation and produces exactly one live Phi.
- Full Debug build and latest CTest: **69/69 passed**, **15.72 seconds**.
- Latest ASan + UBSan Debug: `register_ssa_test`, `register_uses_test`,
  `binding_liveness_test`, `phi_placement_test`: **4/4 passed**, **23.14 seconds**.
- Explicit fixtures cover method-entry self-loops, two-entry irreducible SCCs,
  parallel CFG edges, self-copy read-before-write, Hole guards, legacy lexical
  store input, implicit arguments through v509, normal-unreachable unknown
  instructions, unsupported/exception/resume errors and ownership after input
  destruction and moves.
- An independent path oracle checks **200 generated CFGs x 8 paths x 100 block
  visits = 160,000 visits**. It tracks nearest definitions directly from MOV,
  LDA and branch operands rather than reusing effect extraction. Dynamic write
  generations distinguish different executions of the same loop instruction.
  Phi inputs are selected simultaneously from predecessor versions. Branches
  are nondeterministic CFG choices, not a claim of Ark runtime execution.
- Scale cases: **200, 201, 4,096 and 65,536** distinct referenced registers and
  separately deep dominator chains. Counts verify one instruction naming visit,
  correct previous definitions and bounded work. UINT64_MAX frame size adds no
  frame-sized allocation. No recursive graph traversal is introduced.
- Every short work quota for the diamond is exercised. Memory admission is
  swept at 37-byte intervals, plus exact and one-byte-short peak boundaries.
  Typed failures preserve consumed work and publish no partial result.
- A 1,024-binding chain exercises repeated live-in queries and exhausts a
  shared 1,000,000-unit quota inside IDF/liveness, proving it is not reset per
  binding. This is resource containment, not elimination of quadratic cases.
- Independent review found no production correctness issues. Its suggestion
  to distinguish dynamic loop definitions was incorporated into the oracle and
  both suites rerun. `git diff --check` passes; one worktree remains.

After all build/test/sanitizer processes finished, an isolated run used:

```sh
prlimit --as=1000000000 -- /usr/bin/time -v build/decode/register_ssa_test
```

Exit 0; elapsed **2.94 seconds**; maximum RSS **67,904 KiB = 69,533,696 bytes**.
No child processes are launched by the test. This measures the synthetic SSA
test process under a 1G address-space cap, not full-engine/corpus memory usage.
PMR payload accounting excludes allocator overhead/stack; measured RSS includes
them for this test process.

## Still Incomplete

SSA is not source recovery. It neither proves language-value/Hole/TDZ facts nor
promotes aliased lexical cells; no exceptions/resumption SSA, side-effect
motion, function wrappers or async/lazy task service is supplied by this pass.
The normal CFG must belong to the unchanged validated input body. Actual input
body/CFG and concurrent results remain charged by the owning method service.
SSA has not been connected to the corpus source probe in this increment; no
full-corpus rerun, source-coverage increase or recovered record file is claimed.
