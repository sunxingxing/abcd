# Environment Binding Corpus Validation

Implementation: `42c027e`. Summary: `tests/corpus/environment-bindings.json`.
This measures ordinary environment topology, not source-body readiness,
closure equivalence or recovered records.

Classification manifest SHA256:
`3026acad938cd8a14b38e2e6b22f8c57534900dce7cb9097943729de02fd0d81`.

```sh
cmake --build build/decode-release --target abc_environments -j2
/usr/bin/time -v build/decode-release/abc_environments tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code \
  > build/environment-bindings.json
jq 'del(.results)' build/environment-bindings.json > tests/corpus/environment-bindings.json
```

## Results

- 900 paths: 402 dynamic ABC files and 498 cached static exclusions.
- 5,486,254 decoded methods and normal CFGs; no file/decode/CFG failures.
- 4,922,559 methods accepted; 563,695 rejected for exception regions.
- No other first obstacle occurred. Exceptions are checked first, so this does
  not establish that rejected methods lack resume edges or other problems.
- Accepted methods contain 421,660 environment allocation sites and 7,489,158
  lexical access sites: 5,088,318 captured and 2,400,840 local bindings.
- Accepted analyses report 557,331,702 instruction visits and maximum admitted
  per-method analysis payload of 557,116 bytes, including scratch/index overlap.
  Rejected-method work is excluded from these two counters.
- Whole-process peak RSS: 163,090,432 bytes (159,268 KiB from `/usr/bin/time`).
  Enforced address-space limit: 1,000,000,000 bytes. No child workers.
- Wall time: 62.55 seconds. Exit status: zero, meaning inspection completed.

Global accepted plus rejected equals decoded and CFG method totals. First-error
histogram counts sum to rejected methods. Per-file accepted, rejected, lexical
and captured binding totals were checked against the global summary with jq.

## Paired Corpus

The paired source was not used as engine input or copied into output.

| ABC | Accepted / Methods | Lexical / Captured Sites |
| --- | ---: | ---: |
| Immersive/modules.abc | 632 / 644 | 533 / 382 |
| MultiCommunityApplication/modules.abc | 935 / 936 | 1,021 / 689 |
| pasteboard/modules.abc | 206 / 219 | 210 / 127 |
| push-kit clientdemo modules.abc | 263 / 281 | 143 / 74 |
| push-kit clientdemo widgets.abc | 31 / 31 | 20 / 8 |

## Next Semantic Work

Captured bindings dominate accepted lexical accesses. Connect each ordinary
function creation site through its caller-regional method reference to a
validated target method, retaining its creation environment and target
attributes. Do not equate one method with one closure or merge dynamic
environment instances merely because they share an allocation site. Sendable,
class and historical creation semantics need explicit handling.

Value/Hole/TDZ analysis, exception and suspension scopes, source bindings,
module placement and per-record publication remain necessary. The environment
pass ignores unrelated opcodes for topology purposes, not because those opcodes
are implemented for source emission. No application record is recovered by this
probe, and its RSS is not evidence for the unfinished parallel decompiler.

Verification: Debug CTest 54/54; ASan/UBSan environment flow and index tests 2/2.
