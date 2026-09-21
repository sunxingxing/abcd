# Method Encoding Validation

## Completed Compact-Layout Run

The follow-up at `9595858` reorders operand fields to remove alignment padding:
`InstructionOperand` is 16 bytes instead of 24 and `BodyInstruction` is 144
instead of 192 on the measured x86-64 build. The 64 MiB per-method budget and
1,000,000,000-byte process limit were not raised. A tight-budget regression
failed before the layout change and passed afterward.

With the streaming CLI from `295dbb8`, the same full-corpus command completed
with exit status 0: all 5,486,254 methods decoded, 264,106,514 instructions,
868,111 try regions and 868,114 catch handlers. Peak RSS was 150,249,472 bytes;
elapsed time was 46.95 seconds. No methods failed. The initial four failures
below are retained as historical evidence, not current coverage gaps.

Subsequent normal-flow validation also decodes every method; its per-file
results are preserved in `tests/corpus/normal-flow.json`.

## Initial Run

Reference: pinned OpenHarmony-v7.0-Release ISA. This run validates instruction
encoding and exception boundaries, not recovered source semantics or independent
historical ISA equivalence.

Command (Release):

```sh
/usr/bin/time -v build/decode-release/abc_decode tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code
```

The decoder revision was `4801340`; the corpus inspection CLI is committed with
this report. Release CTest passed all 12 tests.

| Metric | Observed |
| --- | ---: |
| Input files | 900 |
| Dynamic metadata indexes | 402 |
| Cached static classifications skipped | 498 |
| Methods with code | 5,486,254 |
| Successfully decoded methods | 5,486,250 |
| Resource-limited methods | 4 |
| Instructions in successful bodies | 263,350,996 |
| Peak process RSS, bytes | 151,343,104 |
| Address-space ceiling, bytes | 1,000,000,000 |
| Elapsed seconds | 51.05 |

The command correctly exited with status 1: the default 64 MiB method budget
rejected one method in `835b3d19dde2773791afae27f417fc33.abc` (method offset
27769), and three in `e22ec6275b9cc65b027a9b9e141d39d5.abc` (method offsets
7655250, 8073098, 8476869). These are typed `BodyError::kResourceLimit`
failures, not static classifications or successful decompilations. Compact
body storage or bounded spilling remains necessary to improve this coverage.

This sequential validation tool retains only one method body at a time and has
no child workers. Its RSS result does not prove the memory behavior of the
future parallel source-decompilation service. That service must satisfy the
same aggregate process ceiling, including workers, caches, IR and output.
