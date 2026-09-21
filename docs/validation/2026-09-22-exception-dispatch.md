# Exception Dispatch Validation

Implementation: `181910d`, including dispatch core `b2254ec` and hardening
`7ec3146`. Reference semantics and complexity are documented in
`docs/reference/exception-dispatch.md`.

```sh
/usr/bin/time -v build/decode-release/abc_exceptions tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code
```

| Metric | Observed |
| --- | ---: |
| Input files | 900 |
| Dynamic files | 402 |
| Hash-verified cached static exclusions | 498 |
| Decoded methods / normal graphs / dispatch maps | 5,486,254 each |
| Failed files / methods at any stage | 0 / 0 |
| Try entries / catch entries | 868,111 / 868,114 |
| Selected dispatch spans | 976,946 |
| Protected basic blocks | 5,904,939 |
| Protected instructions | 55,308,142 |
| Peak process RSS, bytes | 150,142,976 |
| Address-space limit, bytes | 1,000,000,000 |
| Wall time, seconds | 52.47 |
| Exit status | 0 |

Methods with no local handlers count as successful empty dispatch maps.
Protected coverage means a local handler is selected if an exception occurs;
it is not a may-throw classification or a count of executed exceptions. Splitting
overlapping source ranges may yield more dispatch spans than try entries.

`tests/corpus/exception-dispatch.json` preserves all per-file results, the
implementation revision and the input classification-manifest SHA-256. No ABC
or paired source file was modified or used as generated output. Timing is local
measurement, not a universal throughput guarantee.

## Verification

- Fresh Debug and Release builds with `-Wall -Wextra -Werror`: 18/18 CTest tests
  passed in each configuration.
- ASan/UBSan, tools disabled: 13/13 passed (12 library tests and one CMake pin
  fixture). The limited CLI is tested separately because ASan needs large shadow
  address mappings.
- Tests cover encoded-order versus nesting priority, typed first catch versus
  later catch-all at different PCs, empty lists, half-open/zero ranges, malformed
  slices/boundaries, budgets, a small independent scan oracle, and 200/201/1024/
  8192 overlapping ranges without large quadratic comparison loops.
- CLI tests use real try metadata and cover mixed decode/flow failures in both
  orders; the file summary reports the earliest failed stage, while all method
  diagnostics and per-stage counters remain available.
- Independent specification and quality reviews covered the core, inspection
  extraction and CLI integration.

## Remaining Work

This is not source decompilation or a complete exception-aware SSA graph.
Instruction throw effects and precise abrupt states, entity/literal/module
references, closures, async/generator recovery, bounded asynchronous function
tasks, and verified JS/TS/ArkTS output per record remain. Corpus structure
coverage does not prove historical semantic equivalence or runtime behavior.
