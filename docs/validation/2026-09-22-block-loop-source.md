# Single-Block Loop Source Validation

Design and remaining limitations: [block-loop-source](../design/block-loop-source.md).

## Verification

- Initial valid self-loop test failed against the new API delegating to the
  old acyclic path; it passed after the loop implementation.
- Debug CTest **73/73**, 24.78 s. The old CLI fixture expected rejection of
  all loops; it now checks self-loop acceptance and separately rejects a new
  multi-block cycle fixture. Existing acyclic API still rejects both shapes.
- ASan/UBSan C++ fixtures and generated-source Node tests: **6/6**, 3.13 s
  (`block_loop_body`, `acyclic_body`, `accumulator_flow`, `block_order`).
- Independent review found no outstanding semantic, budget or complexity issue.
- 1,000 small random CFGs compared against an independent DFS cycle oracle;
  source scaling covers 200/201/4,096/65,536 consecutive single-block loops.
- Explicit tests cover first/later iteration accumulator state, Hole backedge
  replay, resetting before reads, forward labels across consecutive loops and
  bypasses, output/nesting/memory limits, and no partial preflight output.
- Node executes both exact branch predicates, repeated Proxy calls without
  property lookups, original thrown-object identity, non-callables, conditional
  bypass and infinite loops terminated by a callee exception. No Ark runtime
  execution or full differential claim is made.

## Full Corpus Probe

```sh
cmake --build build/decode-release --target abc_source_probe -j2
prlimit --as=1000000000 -- /usr/bin/time -v \
  build/decode-release/abc_source_probe tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code \
  > build/source-block-loops.json
jq 'del(.results)' build/source-block-loops.json \
  > tests/corpus/source-block-loops.json
```

The isolated probe exited zero in **90.75 s**, peak RSS **159,588 KiB =
163,418,112 bytes**, address-space ceiling **1,000,000,000 bytes**, no child
workers. Other build/test processes had finished before starting the run.
This validates this probe's memory use, not the unfinished whole engine.

All **900** paths accounted for: **402** dynamic inputs and **498** cached
static exclusions. Indexing and decoding failures remain zero; every non-source
summary field except RSS equals the previous ordinary-call probe. The manifest
SHA-256 is unchanged:
`3026acad938cd8a14b38e2e6b22f8c57534900dce7cb9097943729de02fd0d81`.

| Body Emission Probe | Previous | Current |
| --- | ---: | ---: |
| Accepted | 719,813 | 719,825 |
| Rejected | 4,766,441 | 4,766,429 |
| Bytes rendered into discard sink | 143,242,570 | 143,247,641 |

The 12 additional bodies occur in 10 batch files; no per-file accepted count
regressed. Paired totals remain 323 accepted / 2,111 total. Per-file accepted,
rejected and source-byte sums match global totals; accepted plus rejected equals
all 5,486,254 decoded methods. Sidecar sources were neither read nor copied.

These are body-emission probes with hypothetical ABI-correct local bindings,
not reconstructed function wrappers or complete record source files. No complete
record has been published. Multi-block loops, exceptions, lexical/module/capture
bindings, final function allocation semantics and full record output remain
required work; the modest coverage gain does not change the original goal.
