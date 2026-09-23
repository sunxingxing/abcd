# Constant object loader validation (2026-09-23)

The pinned v7 ISA group has 18 zero-operand forms. The decoded-byte unit test
checks all forms and malformed operand counts. The `linear_body_test`,
`accumulator_flow_test`, `scalar_source_js_test`, `record_object_test` and
`record_object_js_test` verify admitted source behavior, including `ldthis`
receiver identity, `debugger` accumulator preservation and record output.
The six primitive loaders retain the existing scalar Node oracle. Full CTest:
146/146 passed.

Full method-body probe:

```sh
/usr/bin/time -f 'wall_s=%e peak_rss_kib=%M' \
  build/abc_source_probe tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc \
  paired /home/rabbit/sample/abc_with_code \
  > build/source-bodies-constant-v7.json
```

Exit 0, wall 507.36 s, external peak RSS 173,904 KiB (178,077,696 bytes).
The JSON reports 900 files, 402 indexed dynamic files, 498 static skips,
5,486,254 decoded methods, 1,500,484 source-body-ready and 3,985,770
source-body-rejected methods, with zero failed files. Ready/rejected counts
are unchanged from the immediately preceding DEFINE probe. These instructions
therefore have verified semantic coverage but no measured new whole-method
admissions in this corpus. This probe does not validate complete record output,
full-engine peak memory, or equivalence of refused VM-only operations.
