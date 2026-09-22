# Independent Leaf Function Validation

## Change

Straight-line parent bodies can emit verified independent ordinary leaf
functions at their original creation sites. Sorted typed facts permit only
those DEFINEFUNC instructions. Unique target/length keys share prepared source
bytes, including aliased regional slots, but never runtime function objects.
Unsupported captures, home-object operations and deeper creations remain
explicit refusals. Parent cells and instruction evaluation order are retained.

Cache construction admits candidate/fact/site/fragment arrays and all retained
source bytes alongside the current child's decode/analysis allocation. After
construction, temporary candidates die and retained cache storage is deducted
from parent graph/environment/plan allowance. This is per-parent source caching,
not the future bounded asynchronous cross-parent function-task service.

## Tests

- Initial parent fixture failed its success assertion before leaf support.
- Complete ABC CLI positive fixture initially reported ready=1/rejected=1;
  after integration it reports ready=2/rejected=0. A captured child fixture
  rejects both methods with zero source bytes and retains the original parent
  DEFINEFUNC obstacle. Cached static fixture is skipped in every case.
- Debug full build/CTest: 81/81, 52.71 seconds.
- Final strengthened CLI obstacle check: 1/1, 0.28 seconds.
- ASan/UBSan created/base/local-lexical C++ and Node tests: 6/6, 52.36 seconds.
- Node verifies fresh function/prototype identity across parent calls, different
  creation sites even with the same target, child argument/constructor behavior,
  surviving parent cells and different-target source mapping after key sorting.
- C++ verifies source-byte cache sharing for pool aliases, invalid/missing/
  duplicate/reversed/unused facts, captured/nested child refusals, exact/short
  output and scratch budgets, and child work exhaustion at the parent PC.
- Scaling uses 200/201/4096/65536 creation sites and a child with 65536
  instructions, mostly non-emitting NOPs. Child scanning is per unique target,
  not site count; actual output still repeats each function expression.
- Isolated `prlimit --as=1000000000 -- /usr/bin/time -v
  build/decode/created_function_test`: exit 0, 6.07 seconds, peak RSS 45,756 KiB
  (46,854,144 bytes), no children.
- Independent core/CLI review found no concrete production issue. Suggested
  target remapping, work-error PC and unchanged CLI obstacle checks were added.
  Reviewer inspected code/tests but did not independently run the suite.

## Full Corpus

Command:

```sh
prlimit --as=1000000000 -- /usr/bin/time -v \
  build/decode-release/abc_source_probe tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code \
  > build/source-leaf-functions.json
```

Exit 0, 104.56 seconds, no children; peak RSS 160,248 KiB (164,093,952 bytes).
Summary: `tests/corpus/source-leaf-functions.json`.

- 900 paths: 402 dynamic indexed, 498 cached static skips.
- 5,486,254 decoded methods, zero base/decode/lookup failures.
- 720,039 ready bodies, 4,766,215 rejected, 143,308,466 source bytes rendered
  into a discard sink, not published record files.
- Versus source-local-lexicals: +210 bodies and +60,717 bytes in 66 batch
  files; no per-file decline. Paired corpus is unchanged.
- The original accumulator/unsupported opcode 51 (DEFINEFUNC) obstacle falls
  from 41,927 to 41,717. Every other obstacle is unchanged.
- Per-file totals sum to global ready/rejected/bytes; ready+rejected=decoded.
  All other global fields except peak RSS match the previous report.
- Classification manifest SHA-256 remains
  `3026acad938cd8a14b38e2e6b22f8c57534900dce7cb9097943729de02fd0d81`.

This is a real method-body coverage increase, not complete application recovery.
No full record source file or module initialization is produced, no Ark runtime
differential equivalence is claimed, and the future concurrent whole-pipeline
1 GB ceiling remains to be implemented and verified.
