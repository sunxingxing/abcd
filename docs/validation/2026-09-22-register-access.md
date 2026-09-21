# Register Window Corpus Validation

Implementation `3f46bde`; evidence `tests/corpus/register-access.json`.
Reference identities remain pinned to OpenHarmony-v7.0-Release. Classification
manifest SHA256: `3026acad938cd8a14b38e2e6b22f8c57534900dce7cb9097943729de02fd0d81`.

Command:

```sh
/usr/bin/time -v build/decode-release/abc_decode tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code
```

Exit 0. All 402 dynamic files, 5,486,254 methods and 264,106,514 instructions
passed method decoding with the new register-window checks. The 498 positively
classified static paths remained cached exclusions. There were no file or method
failures. Instruction bytes total 712,869,853; the method bodies retain 868,111
try regions and 868,114 handlers.

Elapsed time was 46.63 seconds; peak RSS was 150,487,040 bytes under the enforced
1,000,000,000-byte address-space limit. This measures the validator, not a future
parallel decompilation/output pipeline. Range windows allocate no heap storage
and are not retained by method decoding.

Debug and Release CTest: 28/28 passed. ASan/UBSan CTest: 21/21 passed (tools are
disabled in that build). Focused tests cover all 17 implicit range encodings and
200/201/1024/65535 counts, including receiver/callee additions and the zero-read
argument range. A method fixture demonstrates rejection when only the base is
in bounds and acceptance when arguments extend the frame sufficiently.

This evidence validates bounded register ranges under pinned rules. It does not
prove historical runtime equivalence, complete instruction effects, SSA, source
semantics, or generated per-record files. Accumulator effects still come from
ISA declarations; hidden runtime state and abrupt-exit effects need explicit
semantic modeling before source expressions can be recovered safely.
