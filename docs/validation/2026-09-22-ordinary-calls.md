# Ordinary Call Source Probe

Pinned reference: OpenHarmony-v7.0-Release. See
`../reference/ordinary-calls.md` for supported calls and deliberate refusals.

```sh
cmake --build build/decode-release --target abc_source_probe -j2
/usr/bin/time -v build/decode-release/abc_source_probe tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code \
  > build/source-ordinary-calls.json
jq 'del(.results)' build/source-ordinary-calls.json \
  > tests/corpus/source-ordinary-calls.json
```

Exit zero, 91.16 seconds, peak RSS 163,291,136 bytes. The process enforced a
1,000,000,000-byte address-space ceiling and used no child workers. Builds and
tests overlapped the run; elapsed time is not an isolated performance benchmark.
All 900 paths were accounted for: 402 dynamic files, 498 cached static exclusions.
Base indexing, decoding and CFG failures remained zero. Every non-source
aggregate except RSS matches `tests/corpus/source-named-writes.json` exactly.

| Function Body Probe | Previous | Current |
| --- | ---: | ---: |
| Accepted | 717,670 | 719,813 |
| Rejected | 4,768,584 | 4,766,441 |
| Bytes rendered into discard sink | 142,987,547 | 143,242,570 |

The 2,143 additional accepted bodies are from the batch corpus. Per-file sums
match the global accepted, rejected and byte totals; accepted plus rejected
equals all 5,486,254 decoded methods. Paired acceptance is unchanged: Immersive
126, MultiCommunityApplication 126, pasteboard 30, push-kit modules 36, widgets
5, totaling 323 of 2,111 bodies.

These are actual statement-emission probes with hypothetical ABI-correct local
bindings, not complete functions or application source files. Sidecar source
was not read or copied. No complete record has been published. Lexical/module
bindings, explicit receiver calls, exceptions, loops and wrappers remain
separate work. This run proves the memory bound for this probe only, not the
unfinished whole decompilation pipeline.

Debug CTest passed 64/64. The two ordinary-call tests also passed with
ASan/UBSan instrumentation on the C++ fixture. C++ boundary tests cover zero
count, fixed register 255, range crossing v255, exact/short output budgets,
malformed operands and unsupported calls. Geometric source cases cover
200/201/4096/16384 calls; Node executes emitted calls with 200/201/255 arguments
and adversarial callables. Independent read-only review found no blocking
defect; its suggested cross-v255 case was added. No crafted ABC was executed
in Ark, so these checks do not constitute full runtime differential validation.
