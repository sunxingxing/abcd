# Named Write Source Probe

Pinned reference: OpenHarmony-v7.0-Release. This run uses the modern named-write
emitter and the canonical-element-name guard for both named reads and writes.
The earlier unguarded exploratory run is superseded and is not committed as
validation evidence.

```sh
cmake --build build/decode-release --target abc_source_probe -j2
/usr/bin/time -v build/decode-release/abc_source_probe tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code \
  > build/source-named-writes-guarded.json
jq 'del(.results)' build/source-named-writes-guarded.json \
  > tests/corpus/source-named-writes.json
```

The command exited zero in 87.53 seconds with peak RSS 163,368,960 bytes, below
the enforced 1,000,000,000-byte address-space ceiling. No child workers were
used. Debug/sanitizer builds and tests overlapped the run, so timing is not an
isolated comparative benchmark.

All 900 manifest paths were accounted for: 402 dynamic files and 498 cached
static exclusions. Base indexing/decoding/CFG failures remained zero. Every
previous non-source aggregate field in `tests/corpus/source-bodies.json` matches
exactly, excluding RSS. Newer unrelated inspection fields are present as zero.

| Function Body Probe | Previous | Current |
| --- | ---: | ---: |
| Accepted | 562,473 | 717,670 |
| Rejected | 4,923,781 | 4,768,584 |
| Bytes rendered into discard sink | 37,582,413 | 142,987,547 |

Acceptance increased by 155,197 methods. Per-file accepted/rejected sums match
global counts; together they account for all 5,486,254 decoded methods.

| Paired ABC | Accepted | Rejected |
| --- | ---: | ---: |
| Immersive/modules | 126 | 518 |
| MultiCommunityApplication/modules | 126 | 810 |
| pasteboard/modules | 30 | 189 |
| push-kit clientdemo/modules | 36 | 245 |
| push-kit clientdemo/widgets | 5 | 26 |

Paired acceptance rose from 277 to 323 of 2,111 method bodies. Sidecar source
was not read by the engine or copied into outputs. These counts remain
conditional on supplied ABI-correct names and bindings. The probe emits actual
body syntax into a discard sink, not named application files; it does not prove
wrappers, module placement, captured values, or complete record source.

The current first-obstacle histogram includes 761,917 lexical loads, 603,648
external module loads, 565,262 global name loads and 563,695 exception-region
methods. No candidate reaching emission failed the new indexed-name guard in
this run; first-obstacle reporting does not prove such names are absent behind
other unsupported operations. The guard is covered by explicit boundary tests.

The complete Debug suite passed 62/62 tests. Relevant C++ tests passed ASan/UBSan;
Node executed emitted named writes against strict/sloppy contexts, setters,
Proxies, primitive receivers, unusual UTF-16 names and hygiene collisions.
No crafted ABC was executed in Ark, so pinned path inspection plus JS fixtures
must not be described as full Ark/historical differential validation.
