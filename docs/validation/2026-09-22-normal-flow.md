# Normal Control Flow Validation

Implementation: `b65bb68`, including pinned generated transfers from `714b5b1`
and compact operands from `9595858`. No input ABC or paired source was modified.

```sh
/usr/bin/time -v build/decode-release/abc_flow tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code
```

| Metric | Observed |
| --- | ---: |
| Input files | 900 |
| Dynamic files | 402 |
| Cached static exclusions | 498 |
| Decoded methods / built normal graphs | 5,486,254 |
| Decode failures / graph failures | 0 / 0 |
| Instructions | 264,106,514 |
| Basic blocks | 27,756,181 |
| Normal edges, including resume edges | 29,814,291 |
| Resume edges | 395,295 |
| Peak process RSS, bytes | 150,364,160 |
| Address-space limit, bytes | 1,000,000,000 |
| Wall time, seconds | 49.98 |
| Exit status | 0 |

Machine-readable per-file results are `tests/corpus/normal-flow.json`; its
validation metadata pins the implementation and classification-manifest hash.
Counts include repeated content at distinct corpus paths. Timing reflects this
machine and warm filesystem cache, not a universal latency guarantee.

## Tests and Review

Debug and Release CTest each passed 15/15 tests for the implementation above.
ASan/UBSan passed all 11 library tests with CLI targets disabled, since ASan's
shadow mapping is incompatible with the standalone tools' 1 GB address cap.
Independent specification and quality reviews covered transfer generation,
normal graph construction and the streaming validation CLI.

After registering the build-time ISA pin regression, fresh Debug/Release runs
passed 16/16 tests each and the sanitizer configuration passed 12/12 (11 library
tests plus the non-instrumented CMake pin fixture). Post-configure changes to
each of the three generator inputs were rejected before code generation under
a parallel build; all modifications were confined to temporary copies.

Regressions cover diamond/loop graphs, unreachable blocks, shared conditional
targets, unconditional/conditional throws, generator resume edges, overlapping
exception boundaries, malformed bodies, resource rejection and 200/201/1024/
8192-branch inputs. A 65-failure CLI fixture proves diagnostics are not truncated.

## Limits

This proves encoding and normal-flow structure coverage, not source fidelity.
The graph deliberately contains no exception-dispatch edges; range boundaries
are preserved for a later ordered exception analysis. Resume edges are possible
later activations, not claims that a completed generator resumes. Operand entity
references, literals, modules, SSA, effect order, closure and async semantics,
bounded asynchronous function tasks, and per-record source generation remain.

The corpus does not independently certify every historical ISA semantic profile.
Neither the preserved graphs nor any assembly are delivered as decompiled code.
