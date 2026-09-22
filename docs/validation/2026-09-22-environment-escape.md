# Environment Escape Validation

## Scope

Added conservative allocation-site escape analysis over register SSA and
ordinary lexical environment flow. Alias classes use union/find; directed
containment uses CSR reachability. This is a prerequisite for later cell
promotion, not initialization, TDZ, reaching-store, or source equivalence proof.
It does not authorize deleting environment allocations or merging runtime loop
instances. Numeric representation transformations and raw NaN payloads fail
closed. See `../design/environment-escape.md` for the model and complexity.

## Verification

- Debug full CTest: 74/74 passed, 27.46 seconds.
- ASan/UBSan `environment_escape_test`: 1/1 passed, 10.04 seconds.
- Initial lexical-cell alias regression failed against the deliberate unsupported
  stub before the implementation passed it.
- Independent review found a missing indexed Phi-query term in the documented
  complexity; the bound was corrected. Final review reported no remaining
  concrete issue. The reviewer did not independently rerun the full suite.

Tests cover self-cell aliases, copy aliases, captured stores, explicit deprecated
store registers, sparse wide slots, directional containment, parent/child escape,
creation capture, sibling Phi alternatives, loops, opaque numeric rejection,
ownership after input destruction, moves, and exact work/memory budgets.
A separate graph/BFS oracle checks 500 small randomized lexical programs without
reusing the production union/find solver. Scaling fixtures cover 200, 201, 4096
and 65536 environments, including unobserved cyclic cells and escaping chains.

## Memory

After other builds and tests finished, this isolated command exited zero:

```sh
prlimit --as=1000000000 -- /usr/bin/time -v build/decode/environment_escape_test
```

Elapsed time: 2.71 seconds. Maximum RSS: 60016 KiB (61456384 bytes).
The process includes test fixture construction, register SSA, environment flow
and escape analysis; it launches no child workers. The address-space limit is
1000000000 bytes. Counted scratch tests additionally exercise exact admission
and one-byte-under-budget rejection without publishing a partial result.

This verifies only this synthetic workload, not whole-engine or concurrent
full-corpus memory compliance. Caller-owned input memory remains separately
admitted under the analysis API contract.

## Coverage Boundary

No source emitter or corpus classification was changed, so no source coverage
increase or new full-corpus run is claimed. The latest source probe remains
`tests/corpus/source-block-loops.json`. No complete application record source
file has been recovered. Cell initialization/value recovery, function wrappers,
record publication and the bounded asynchronous function service remain work
for subsequent increments.
