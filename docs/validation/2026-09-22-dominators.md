# Normal Dominator Corpus Validation

Implementation `1a398b2`; evidence `tests/corpus/dominators.json`.
Classification manifest SHA256:
`3026acad938cd8a14b38e2e6b22f8c57534900dce7cb9097943729de02fd0d81`.

```sh
/usr/bin/time -v build/decode-release/abc_dominators tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code
```

Exit 0; 402 dynamic files inspected, 498 cached static exclusions. All 5,486,254
methods passed decoding, normal graph construction and dominator construction.
There were no failures at any exercised stage.

- 27,756,181 blocks; 29,814,291 normal edges, including 395,295 resume edges.
- 26,286,691 blocks reachable from their method entry in the supplied graph.
- 147,101,122 instrumented edge visits; 13,835,160 path-compression steps.
- Largest one-method dominator result plus scratch capacities: 418,208 bytes.
- Whole-process peak RSS: 150,204,416 bytes; enforced address-space limit:
  1,000,000,000 bytes. Elapsed time: 53.69 seconds.

Debug and Release CTest passed 30/30; ASan/UBSan passed 22/22 with tools disabled.
The unit oracle independently removes vertices and checks reachability for all
343 permitted three-node graphs at every entry, plus 1,000 seeded 16-node graphs.
Deep-chain/backedge and exact-budget tests cover up to 65,536 blocks. Integration
checks distinguish ordinary reachability from exception-only handler entry.

The 1,469,490 normally unreachable blocks MUST NOT be treated as dead code:
exception dispatch is absent from this graph, and resume edges denote later
activation. The corpus run establishes successful bounded construction, not an
independent dominator oracle for every real graph. It does not establish SSA,
exceptional definitions, source semantics, generated per-record files, or the
memory usage of the future complete parallel decompiler.
