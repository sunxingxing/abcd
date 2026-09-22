# Register SSA Construction

## Contract

Compose semantic register uses, live-in and pruned IDF with a single iterative
dominator traversal. Inputs are a validated method, its unchanged normal CFG,
frame size and shared method quota. Reject exceptions/resumes and reachable
instructions without verified register effects. Own every published result.
An entry value means an unknown incoming VM slot, NOT JS undefined or a proven
language value. Lexical cells, Hole, environment identities and captures still
need separate value/alias analysis; SSA does not authorize moving effects.

Choose conventional dominance-based renaming over repeated reaching-definition
sets (potentially quadratic) or sealed-block recursive construction (would add
a second phi strategy and recursion/trivial-phi dependency machinery). Reuse
the implemented pruned IDF, with its documented cross-variable limitations.
The naming algorithm is independently implemented, no LLVM source copied.
Reference: LLVM llvmorg-19.1.7
[PromoteMemoryToRegister.cpp](https://github.com/llvm/llvm-project/blob/llvmorg-19.1.7/llvm/lib/Transforms/Utils/PromoteMemoryToRegister.cpp)
for established promotion/phi/renaming boundaries; this implementation uses
explicit dominator intervals and rollback rather than duplicating its pass.

## Representation and Algorithm

- Strong instruction/value IDs, typed origin variants: method entry,
  instruction definition, block Phi. Values also retain their register/acc slot.
- Flat sorted read/write sites and Phi inputs. Phi input origin is either a
  real source block/successor ordinal or synthetic method entry. Duplicate CFG
  edges stay distinct. Input order is deterministic. No strings encode meaning.
- Build RegisterUses and one IDF index, query only bindings with both definitions
  and upward uses. Keep one quota across every binding and stage. Sort Phi sites
  by block/binding and construct block offsets in O(B+P).
- Build the dominator tree once for renaming. Visit its preorder iteratively;
  a current-value array is sized by referenced bindings, not declared frame.
  Enter Phi definitions, process reads before each instruction's write, and add
  outgoing Phi operands. Undo only actual writes on leaving each subtree.
- Allocate all Phi value IDs before traversal so forward/back edges refer to
  stable values. A Phi in entry block 0 gets an explicit method-entry operand
  in addition to backedges. This is essential when the CFG entry is a loop.
- Sort output reads/writes by instruction and inputs by Phi/edge. Read/Write
  lookup is logarithmic plus returned span; no result borrows scratch or inputs.

Let R be referenced slots, U accesses, P Phi nodes and A Phi operands. Renaming
costs O(B+E+(U+A)log(2+R)+(U+A)log(2+U+A)), plus the existing uses, dominance,
liveness and IDF costs. Rollback is O(U+P), never an R-by-B state copy. Method
quota bounds repeated queries, not an asymptotic proof for the whole pipeline.
The upstream dominance/index constructors are bounded single builds, not
interruptible query loops; charge block admission once per constructor.

## Memory

One PMR accounting resource covers local vectors including growth overlap.
Externally owned intermediate results reserve their retained bytes; their
reported temporary peaks are observed against remaining budget before further
allocations. Every nested operation receives only available bytes. Final owned
arrays are admitted while builder scratch remains live. Input method/CFG bytes
and concurrent workers remain the caller's responsibility. Byte budget defaults
to 64 MiB; whole-engine 1,000,000,000-byte enforcement remains a service concern.

## Implementation and Verification Plan

1. Add `tests/unit/analysis/register_ssa_test.cc` and CMake target. A decoded
   diamond must read r1's Phi at its join and preserve both predecessor values;
   run `cmake --build build/decode --target register_ssa_test -j2` and observe
   failure before the production header exists.
2. Add `include/abcd/analysis/register_ssa.h`, private
   `src/analysis/register_ssa_builder.h`, orchestration in `register_ssa.cc`
   and dominator traversal in `register_rename.cc`. Extend private
   `idf_scratch.h` only for external retained-byte/peak accounting.
3. Verify loops, entry backedges, same-register moves, duplicate CFG edges,
   dominance rollback between siblings, implicit call ranges, dead instructions
   and typed failures. Use a bounded concrete path oracle with manually tracked
   registers, not a second implementation calling the same renaming code.
4. Test 200/201/geometric scaling, deep traversal without recursion, shared work
   quota and exact/short memory limits, ownership/moves, Debug and ASan/UBSan.
   Review independently, measure RSS under a 1G address-space cap, record
   evidence in docs/validation and commit the verified increment. Do not claim
   source coverage, corpus completion or completed function scheduling.
