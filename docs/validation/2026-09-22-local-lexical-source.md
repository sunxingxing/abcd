# Local Lexical Source Validation

## Implemented

Added `LinearLexicals` and `WriteLocalLexicalBody`: a straight-line initialization
proof and real JS local declarations/assignments/loads. Language-only register
entry assumptions remain caller obligations. Internal environment/Hole values
and their copies may be omitted only while all subsequent observable reads are
checked. Uninitialized/captured cells, possibly failing guards, named pushes,
captured pop/current, implicit function capture, handlers and non-linear control
flow are refused. GC/OOM timing and debugger introspection are outside this
allocation-omission model; no failed guard is replaced by undefined or erased.

The private single-instruction renderer reuses existing source semantics. The
complete method is preflighted before output. Actual cell keys are indexed;
declared slot counts do not expand storage. See `../design/local-lexical-source.md`
for ownership, source binding, complexity and resource contracts.

## Tests and Review

- The initial source test failed against a WriteLinearBody delegating stub,
  then passed with exact JS declarations/store/load/return text.
- The new CLI positive fixture failed with `no source rendered` before fallback
  integration. It passes afterward; the uninitialized negative fixture remains
  rejected. Additional assertions enforce mutually exclusive ready/rejected
  counts, zero bytes on rejection and unchanged original obstacle attribution.
- Complete Debug build and CTest: **77/77 passed**, **37.63 seconds**. The final
  strengthened CLI test separately passed in **0.23 seconds**.
- ASan/UBSan: local lexical, linear, constant, acyclic and block-loop C++ tests
  plus corresponding JS execution fixtures: **9/9 passed**, **8.90 seconds**.
- Node executes recovered bodies with undefined/null/booleans/numbers/negative
  zero/NaN/infinity/BigInt/symbols/objects. It checks identity, parent/child same
  slot separation, pop behavior, deprecated explicit-register stores, wide
  slots, conditional diagnostic-name non-reading, stale internal copies,
  indirect-call receiver/arguments, absence of callee property reads, and exact
  propagated exception identity. These are source fixtures, not Ark execution
  of complete applications or synthesized production function wrappers.
- Negative tests cover uninitialized/internal/captured reads, unsafe guards,
  named pushes, capture, unknown call results stored to cells, branches, missing
  or early terminators, binding collisions, output budgets and stream failures.
- Plan work quotas, exact/one-byte-short memory budgets, owned results after
  input destruction and moved-from state are tested. Sparse 65535-slot
  allocations with one accessed cell scale through **200/201/4096/65536**
  distinct environments without expanding declared slots or per-cell CFG walks.
- Independent design review identified captured-pop and named-push omissions
  that required additional evidence; both are explicitly rejected here. Final
  implementation and fallback reviews reported no concrete issue. Suggested
  lifetime and counter-exclusivity tests were added. Reviewer did not run tests.

## Isolated Memory

After builds/tests finished:

```sh
prlimit --as=1000000000 -- /usr/bin/time -v build/decode/local_lexical_body_test
```

Exit **0**, **1.97 seconds**, peak RSS **130104 KiB = 133226496 bytes**, no children.
This includes fixtures and captured source strings, not just allocator counters.

## Full Corpus

Release probe, after other builds/tests finished:

```sh
prlimit --as=1000000000 -- /usr/bin/time -v \
  build/decode-release/abc_source_probe tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code \
  > build/source-local-lexicals.json
jq 'del(.results)' build/source-local-lexicals.json \
  > tests/corpus/source-local-lexicals.json
```

Exit **0**, **94.67 seconds**, peak RSS **159916 KiB = 163753984 bytes**.
Configured address-space cap: **1000000000 bytes**. No child workers.

- 900 paths: 402 dynamic, 498 cached static skips.
- 5486254 decoded methods; zero base/decode failures.
- **719829** source-body-ready, **4766425** rejected.
- **143247749** source bytes rendered into the probe's discard sink, NOT record
  source files written to disk.
- Versus `source-block-loops.json`: **+4 bodies**, **+108 bytes**, across three
  batch files; zero per-file regressions. All four were previously blocked on
  NEWLEXENV (opcode 9). Other obstacle counts are unchanged.
- Batch deltas: `017615cd2d24a72b15bb6503b629d646.abc` +1,
  `d16ea822a38173e00972a975dedb3f7f.abc` +2,
  `f28faf17103f0675fecb6202ea8bf156.abc` +1.
- Paired source-body coverage is unchanged: 323/2111 methods. Sidecar source was
  neither read nor copied.
- Per-file ready/rejected/byte sums match totals; ready + rejected = decoded.
  All non-source summary fields except measured RSS match the previous probe.
- Classification manifest SHA-256 unchanged:
  `3026acad938cd8a14b38e2e6b22f8c57534900dce7cb9097943729de02fd0d81`.

## Remaining Goal

This is a small verified body-coverage increase, not complete application
decompilation. No complete application record source file has been recovered.
Captured environments, creation/wrappers, general control flow/TDZ/exception
semantics, per-record publication and default bounded async/lazy execution
remain incomplete. The measured single-worker probe does not establish the
future whole-engine concurrent 1G limit.
