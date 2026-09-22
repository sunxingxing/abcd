# Initialized Capture Validation

## Change

Ordinary one-level leaf functions now share proven initialized parent-local
cells through native JS free-variable bindings. Typed child capture holes are
normalized through child environment analysis, then mapped separately at each
parent creation site. Parent initialization and all parent/child stores preserve
the Language invariant. No textual replacement or snapshot capture is used.

Unique target/length keys share immutable literal templates and typed holes,
not parent owners or runtime objects. Parent graph/environment storage, child
preparation, template arrays and per-site mapping rows share the admitted budget.
The public standalone created-function API still refuses unbound capture.

## Tests

- The initial initialized-capture success assertion failed before implementation.
- Debug full build and CTest: 81/81 passed, 51.20 seconds.
- ASan/UBSan created/base/local-lexical C++ and Node tests: 6/6 passed,
  53.37 seconds.
- Node verifies parent writes after creation, calls after parent return,
  sibling getter/setter sharing, callback writes observed by the parent,
  reentrant factory isolation, separate parent invocations, two creation
  environments for one target, child-local shadowing and ancestor capture.
- C++ checks early creation before initialization, missing outer owner, slot
  bounds, internal captured stores, public unbound-capture refusal, nested
  creation refusal, exact/short expanded output and captured scratch budgets,
  stream failure, shared-template distinct mapping rows and move stability.
- Scaling uses 200/201/4096/65536 creation sites sharing one 65536-instruction
  captured child, mostly non-emitting NOPs. Repeated analysis is per unique
  target, not per site; capture mappings and source expansion are output-sized.
- Complete ABC CLI fixture recovers the initialized parent while still rejecting
  its child in isolation. The old missing-parent-env fixture remains rejected
  with its original DEFINEFUNC obstacle; static fixture remains skipped.
- Isolated test under `prlimit --as=1000000000`: exit 0, 6.22 seconds,
  peak RSS 50,240 KiB (51,445,760 bytes).
- Independent design/code review found no concrete soundness or budget issue.
  Suggested capture scratch, mapping-scale and move checks were included.
  Reviewer inspected code but did not independently execute the suite.

## Full Corpus

```sh
prlimit --as=1000000000 -- /usr/bin/time -v \
  build/decode-release/abc_source_probe tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code \
  > build/source-initialized-captures.json
```

Exit 0, 105.54 seconds, no probe children. Peak RSS 159,392 KiB
(163,217,408 bytes). Summary: `tests/corpus/source-initialized-captures.json`.

- 900 paths, 402 dynamic indexed, 498 cached static skips.
- 5,486,254 decoded methods; zero base/decode/lookup failures.
- 720,079 ready bodies, 4,766,175 rejected, 143,326,578 discarded source bytes.
- Against `source-leaf-functions.json`: +40 ready bodies, +18,112 source bytes,
  across eight batch files, no per-file coverage decline.
- Paired corpus unchanged: 323 ready, 1,788 rejected.
- Only obstacle change is accumulator/error 3/opcode 9:
  415,889 to 415,849. Other global fields except source totals/obstacles/RSS
  are unchanged. Per-file identities/order and total sums were checked;
  ready plus rejected equals decoded methods.
- Classification SHA-256 unchanged:
  `3026acad938cd8a14b38e2e6b22f8c57534900dce7cb9097943729de02fd0d81`.

These are method-body probe results, not published record source files or an
Ark differential equivalence claim. Creation before initialization, deeper
closures, general CFG, module initialization and record publication remain
unsupported here. The default asynchronous function service and its aggregate
whole-pipeline 1 GB bound are still required; this measurement proves only the
current tested process paths.
