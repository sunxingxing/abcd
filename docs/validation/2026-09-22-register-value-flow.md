# Register Value Categories Validation

## Implemented Boundary

RegisterValueFlow computes owned finite sets of possible VM value categories
over RegisterSsa: language value, Hole, internal environment and other internal
value. Copies preserve categories; Phis incrementally union dependencies. Entry
facts are explicit caller-proven assumptions; unspecified entries remain top.
Calls, properties, lexical loads and current environment remain unknown without
callee/heap/cell/scope evidence. Hole checks do not globally refine an SSA value.

The CSR worklist visits each dependency edge at most four times. It never
rescans a Phi's entire input list per notification and has no recursive graph
traversal. Extraction uses indexed SSA lookups; CSR construction/solving are
linear in values plus dependencies for this fixed-height domain. The method
quota is shared and failures retain consumed work. PMR accounts growth overlap
and the final owned array while scratch remains live; input body/SSA ownership
is charged separately by the caller.

## NaN Soundness Fix

Independent review found two counterexamples in the initial uncommitted rules:
raw FLDAI bits 0xffff000000000005 can become Hole under the pinned assertion-
disabled tagged-double constructor; accepted positive NaN 0x7fff000000000005
can become the same pattern under NEG. Both tests were observed failing before
their respective fixes and now pass. Evidence and pinned source locations are
recorded in `docs/design/register-value-flow.md`; no Ark binary was executed.

Impure FLDAI encodings now stay top. Numeric/bitwise/conversion outputs also
remain top until stronger numerical evidence exists. Only reviewed boolean/
string-producing scalar operations can infer Language from Language operands.
Creation and NEWLEXENV normal-completion result categories were checked against
their pinned allocation paths. A category fact does not establish JS source
equivalence, exact numerical behavior, effect purity or safe expression motion.

## Verification

- Initial target failed for its missing production header; the Hole copy-chain
  test passes after implementation.
- Full latest Debug build and CTest: **70/70 passed**, **20.69 seconds**.
- Latest ASan + UBSan Debug: register_ssa_test and register_value_flow_test,
  **2/2 passed**, **26.97 seconds**.
- **500 generated small CFGs** match an independent opcode-specific full-sweep
  fixed-point oracle. This is an analysis oracle, not an Ark execution oracle.
- Loop propagation covers all **15 nonempty category subsets**, explicit entry
  assumptions and duplicate/invalid fact rejection. Tests also cover unknown
  calls/properties/loads, environment and Hole copies, creation, comparisons,
  guard non-refinement, moved-from access and input destruction.
- Copy chains and high-fan-in joins scale through **200, 201, 4,096, 65,536**
  choices; the largest Phi has **65,537 inputs**. Edge-visit/change counters
  enforce the four-atom bounds. Large fixture SSA construction has a separate
  256 MiB allowance; value flow itself retains its default 64 MiB budget.
- Every insufficient work quota for the initial fixture fails with typed work
  exhaustion. Exact work and byte budgets pass; one-byte-short peak fails with
  resource exhaustion. No partial facts are published on failure.
- Final independent review found no remaining correctness issue or blocking
  test gap. `git diff --check` passes; one worktree, no sample modifications.

After builds, CTest and sanitizer processes finished:

```sh
prlimit --as=1000000000 -- /usr/bin/time -v build/decode/register_value_flow_test
```

Exit 0, elapsed **4.40 seconds**, maximum RSS **115,860 KiB = 118,640,640 bytes**.
The test launches no child processes. This measures the synthetic test process
under a 1G address-space cap, not the complete engine or full corpus. PMR counts
exclude allocator/stack overhead; RSS includes them for this test process.

## Still Incomplete

No source-probe integration or full-corpus rerun occurred. No record source file
or source-coverage increase is claimed. Numerical constraints, use-site guard
refinement, lexical-cell alias analysis, function wrappers, exception/resumption
support and the bounded async/lazy method service remain separate work.
