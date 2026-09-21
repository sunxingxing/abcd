# Creation Binding Corpus Validation

Evidence: `tests/corpus/creation-bindings.json`. Implementation is the commit
introducing this report. This is method-target/capture-context binding, not
closure-value recovery, generated source or recovered records.

Classification manifest SHA256:
`3026acad938cd8a14b38e2e6b22f8c57534900dce7cb9097943729de02fd0d81`.

```sh
cmake --build build/decode-release --target abc_creations -j2
/usr/bin/time -v build/decode-release/abc_creations tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code \
  > build/creation-bindings.json
jq 'del(.results)' build/creation-bindings.json > tests/corpus/creation-bindings.json
```

## Results and Scope

- 900 paths: 402 dynamic files and 498 cached static exclusions.
- 5,486,254 decoded methods/normal CFGs; no file, method lookup, decode or CFG
  failures. Source-paired files were not read as engine inputs.
- 4,922,559 methods admitted to creation inspection; 563,695 refused because
  environment analysis rejects exception regions. Sites in those methods are
  not counted. This is not proof of equivalent behavior in accepted methods.
- 2,780,261 modern creation sites bound; zero target failures and zero
  normal-unreachable modern sites within inspected methods.
- Bound sites: 1,652,439 ordinary local captures, 1,127,818 ordinary external
  captures and 4 sendable DEFINEFUNC captures.
- Other method-bearing instructions: 206,471 narrow DEFINECLASSWITHBUFFER,
  3,944 wide DEFINECLASSWITHBUFFER and 4,463 CALLRUNTIME_DEFINESENDABLECLASS.
  These 214,878 instructions are not implemented by the modern creation binder.
  The inventory does not enumerate methods hidden inside literal arrays.
- Whole-process peak RSS: 163,606,528 bytes (159,772 KiB from `/usr/bin/time`).
  Enforced address-space limit: 1,000,000,000 bytes. No child workers.
- Wall time: 70.45 seconds. Exit zero means inspection completed, not recovered
  application sources.

Verified with jq: admitted plus rejected methods equals decoded methods; bound,
failed and unreachable sites partition creation sites; ordinary local, ordinary
external and sendable categories partition bound sites. Obstacle counts equal
rejected methods plus failed sites. Per-file site, bound and rejected-method
totals agree with global totals. The mixed-unit obstacle histogram must not be
interpreted as a failed-method count.

## Paired Files

| ABC | Bound Sites | Local / External Captures |
| --- | ---: | ---: |
| Immersive/modules.abc | 402 | 152 / 250 |
| MultiCommunityApplication/modules.abc | 765 | 317 / 448 |
| pasteboard/modules.abc | 151 | 47 / 104 |
| push-kit clientdemo modules.abc | 164 | 85 / 79 |
| push-kit clientdemo widgets.abc | 25 | 11 / 14 |

No sendable DEFINEFUNC sites were encountered in these five inspected paired
files. This is not an absence proof for excluded methods or class creation.

## Verification and Remaining Work

Full Debug CTest passed 57/57; the new creation-binding unit test passed
ASan/UBSan. An independent read-only review found no implementation correctness
issue. Its unreachable CLI coverage gap was filled; direct CLI coverage for
sendable counters and lookup construction failure remains absent, with the
underlying operations covered by unit tests.

The next dependency is cross-function captured-binding substitution, not a
method-to-one-parent shortcut. Distinct sites and fresh loop instances must
remain distinct. Value/Hole/TDZ recovery, source closures, class/literal-buffer
creation, exceptions/resumption and per-record publication remain required.
This synchronous probe's RSS does not establish the unfinished parallel
decompiler's complete memory bound. No recovered record is claimed.
