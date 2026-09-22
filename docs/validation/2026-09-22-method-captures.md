# Method Capture Construction Corpus Validation

Reference: pinned OpenHarmony-v7.0-Release. Inputs are the existing hash-checked
900-path manifest, including 402 dynamic and 498 cached static exclusions.
Paired source was not an engine input. Aggregate evidence is committed at
`tests/corpus/method-captures.json`; detailed output is in the untracked build
directory and can be regenerated:

```sh
cmake --build build/decode-release --target abc_creations -j2
/usr/bin/time -v build/decode-release/abc_creations tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code \
  > build/method-captures.json
jq 'del(.results)' build/method-captures.json > tests/corpus/method-captures.json
```

The command exited zero in 76.53 seconds. Peak RSS was 163,213,312 bytes with
an enforced address-space ceiling of 1,000,000,000 bytes and no child workers.
The Debug regression suite overlapped the first seconds of this run; timing is
not an isolated performance comparison.

All 5,486,254 methods decoded and built normal CFGs. The environment analysis
accepted 4,922,559 and rejected 563,695 at its exception-region boundary. Every
accepted method constructed an owned `MethodCaptures` result; construction
failures were zero. Maximum successful result-build payload was 161,060 bytes.
Results were released per method, not retained across the corpus.

All previous aggregate fields except RSS exactly match
`tests/corpus/capture-summaries.json`, including 2,780,261 bound creation sites,
5,088,318 direct captured accesses and 2,542,362 per-method distinct captured
slots. Per-file result/failure sums match global totals and every file obeys
`results + failures = creation_methods`.

| Paired ABC | Results | Environment Rejections | Max Payload Bytes |
| --- | ---: | ---: | ---: |
| Immersive/modules | 632 | 12 | 784 |
| MultiCommunityApplication/modules | 935 | 1 | 1,472 |
| pasteboard/modules | 206 | 13 | 988 |
| push-kit clientdemo/modules | 263 | 18 | 1,032 |
| push-kit clientdemo/widgets | 31 | 0 | 768 |

The CLI test first failed because the new fields did not exist. After integration
it checks result/failure partitioning, missing-target category/count, successful
nonempty payloads, unreachable sites, environment refusals and static exclusions.
The full Debug suite passed 59/59 tests. Independent read-only review found no
correctness issue; suggested histogram/peak assertions were added and passed.

This validates owned-result construction and bounded sequential lifetime on
real ABCs. It does not validate all-corpus cross-method Resolve queries,
transitive capture propagation, runtime cell identity, async cache behavior,
value/TDZ semantics, complete generated records or whole-pipeline memory.
Exception-bearing methods and unhandled class/sendable semantics remain work,
not successful decompilation.
