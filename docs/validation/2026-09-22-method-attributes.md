# Method Attribute Corpus Validation

Implementation `a4bd214`; evidence `tests/corpus/method-attributes.json`.
The manifest is the same SHA256-pinned input as `2026-09-22-frames.md`.

```sh
/usr/bin/time -v build/decode-release/abc_frames tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code
```

Exit 0. All 402 dynamic files and 5,486,254 methods pass call-type, attribute
and declared-frame checks; 498 static entries are skipped using cached evidence.
Initial function kinds, in the serialized enum order:

| Initial Kind | Methods |
| --- | ---: |
| None | 2,587,264 |
| Base constructor | 844,523 |
| Arrow | 1,756,104 |
| Generator | 22 |
| Async | 249,588 |
| Async generator | 4 |
| Async arrow | 46,290 |
| Concurrent | 2,459 |

Four methods carry the independent sendable flag. All call-type reads still
select the missing-element default; aggregate frame counts match the earlier
run. Peak RSS is 162,951,168 bytes (159,132 KiB), with the 1,000,000,000-byte
address-space limit enforced. Elapsed time is 42.13 seconds. This is an
inspection-process memory observation, not final parallel-decompiler validation.

Debug and Release tests passed 41/41; the new exhaustive attribute unit test
passed ASan/UBSan. Region indexes are decoded but this run does not compare
encoded indexes to the method's spatial region or alter constant resolution.

The prevalence of None and the presence of concurrent/sendable kinds require
creation-site and runtime-specific recovery before choosing source wrappers.
Initial kind decoding alone does not prove final function semantics, historical
engine equivalence, function length, lexical binding, or constructibility.
No per-record source files are produced by this inspection.
