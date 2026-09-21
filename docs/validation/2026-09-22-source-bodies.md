# Source Body Corpus Probe

Evidence: `tests/corpus/source-bodies.json`. This is body-emitter eligibility,
**not recovered application source or record-level semantic equivalence**.
The implementation is the commit introducing this document. Reference runtime
and ISA identities remain pinned by the existing release lock.

Classification manifest SHA256:
`3026acad938cd8a14b38e2e6b22f8c57534900dce7cb9097943729de02fd0d81`.

```sh
/usr/bin/time -v build/decode-release/abc_source_probe tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code \
  > build/source-body-probe-final.json
jq 'del(.results)' build/source-body-probe-final.json > tests/corpus/source-bodies.json
```

## Scope and Results

Exit 0 means the inspection completed, not that rejected bodies were recovered.
Input/hash/header/index/decode failures still cause a nonzero exit. Semantic or
structuring refusals are expected probe outcomes counted separately. Static
exclusions reuse the validated manifest; source-paired files are never consulted.

- 900 paths: 402 dynamic ABC files and 498 cached static exclusions.
- 5,486,254 methods decoded; no decoding or file inspection failures.
- 562,473 bodies passed the current writer; 4,923,781 were rejected.
- 37,582,413 source bytes rendered successfully into a sink, not retained.
- Whole-process peak RSS: 163,364,864 bytes. Enforced address-space limit:
  1,000,000,000 bytes. No child workers. Wall time: 78.25 seconds.
- Per-file totals sum to global totals; the obstacle histogram sums to rejected
  bodies, and ready plus rejected equals decoded methods.

The five paired ABC files contain 2,111 methods, with 277 ready and 1,834 rejected:
`Immersive/modules.abc` 111/644, `MultiCommunityApplication/modules.abc` 107/936,
`pasteboard/modules.abc` 22/219, push-kit `modules.abc` 32/281 and `widgets.abc`
5/31. These are body eligibility counts, not paired-source equivalence checks.

The writer checks call-type/frame layout and uses unique hypothetical local
register bindings, then invokes the actual acyclic source path, including
constant resolution, preflight and streaming. It does not declare/initialize
those bindings or recover wrappers. Body readiness is therefore conditional on
ABI and scope recovery, not proof that a standalone function is semantically
correct. Historical runtime equivalence and Ark execution remain unverified.

## Next Work From Evidence

Each rejected method contributes only its first obstacle in analysis order.
These are not total opcode frequencies or predicted incremental success counts;
removing one obstacle can expose another. The largest observed categories are:

| First obstacle | Methods |
| --- | ---: |
| `LDLEXVAR_IMM4_IMM4` | 759,060 |
| `LDEXTERNALMODULEVAR_IMM8` | 587,415 |
| Exception regions | 563,695 |
| `TRYLDGLOBALBYNAME_IMM8_ID16` | 558,344 |
| `CALLTHIS0_IMM8_V8` | 475,596 |
| `NEWLEXENV_IMM8` | 415,889 |
| `CALLTHIS1_IMM8_V8_V8` | 410,732 |
| `STOBJBYNAME_IMM8_ID16_V8` | 255,268 |
| `LDLOCALMODULEVAR_IMM8` | 128,836 |
| `CREATEOBJECTWITHBUFFER_IMM8_ID16` | 119,008 |
| Reachable cycles | 91,294 |

46,829 methods also encounter accumulator state not proven language-valued at
`STA_V8`; this must be investigated as internal-value propagation, not hidden by
initializing the accumulator to JS undefined. Five otherwise supported methods
exceed the default label nesting budget.

Prioritize binding/capture identity, module/global resolution and call/receiver
semantics, with real method fixtures. Loop and exception recovery remain required.
Do not optimize the displayed ratio by suppressing obstacles or counting partial
bodies as complete records. No per-record files are produced by this tool.

## Resource and Regression Contracts

One decoded body and its source analyses are live at a time. Fixed-width register
names and views have a separate 64 MiB payload budget admitted before allocating
fixed arrays; allocator overhead remains inside the process ceiling. There is no
per-name allocation or body-sized source buffer. Histograms use typed error
variants and optional opcode keys. Only JSON serialization uses category text.
The histogram contains first obstacles, not an unbounded list of method errors.
Instruction lookup for an obstacle is binary search, not a repeated method scan.

CLI tests cover successful return and regional-string rendering, frame underflow, unsupported array
creation, cycles, exception regions, name-budget exhaustion, malformed bytecode
and static exclusions.
Cycles and exception regions have no misleading opcode attribution. Full Debug
CTest passed 51/51; the new CLI test also passed in Release. Three full-corpus
runs produced identical readiness counts, source bytes and obstacle histograms.
The memory result applies to this synchronous probe only,
not the unfinished async/parallel full-record decompiler.
