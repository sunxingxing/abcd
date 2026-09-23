# DEFINE source progress, 2026-09-23

Revision: `323a0b7` (after `6247094`, `11f8781`, and `0d1a82a`).
Reference: pinned `OpenHarmony-v7.0-Release` in
`docs/reference/openharmony-v7.0-release.lock.json`.

## Verified behavior

- All 14 v7 DEFINE-related wire forms have typed operands and checked
  normal-completion register effects. This does not prove source equivalence.
- A proved fresh, unescaped object in a linear function body can emit named
  `DEFINEFIELDBYNAME`/`DEFINEPROPERTYBYNAME`, encoded unsigned-index
  `CALLRUNTIME_DEFINEFIELDBYINDEX`, and
  `CALLRUNTIME_DEFINEFIELDBYVALUE` with a still-live `LDA_STR` key. The last
  form refuses unknown and overwritten keys. Other DEFINE source admission
  remains as listed in `docs/design/define-instructions.md`.
- `ctest --test-dir build --output-on-failure -j4`: 143/143 passed. The
  `object_buffer_js_test` Node oracle covers own descriptors, inherited setter
  exclusion, `__proto__`, numeric key order, and the maximum unsigned key.
- `abc_structure` on paired Immersive `modules.abc` emitted 5 of 45 executable
  record files; process peak RSS was 11,141,120 bytes. This sample is not a
  full-corpus memory proof or evidence that other records are complete.

## Full-corpus body probe

Command:

```sh
/usr/bin/time -f 'wall_s=%e peak_rss_kib=%M' \
  build/abc_source_probe tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc \
  paired /home/rabbit/sample/abc_with_code \
  > build/source-bodies-define-v7.json
```

Exit 0; wall time 437.18 s; external peak RSS 173,964 KiB. Probe JSON reports
900 files, 402 indexed dynamic files, 498 static skips, zero failed files and
methods, 5,486,254 decoded methods, 1,500,484 source-body-ready methods,
3,985,770 rejected methods, and 178,139,136 peak RSS bytes. The source-body
ready count increased by two from the saved same-stage baseline
`build/source-bodies-current.json` (1,500,482); one method each was gained in
`4655acad3603c111523c7c96488f06bf.abc` and
`4790b5ff4983aa621f04ce11ed6da4e3.abc` in the batch corpus.

The body probe tests per-method eligibility, not complete ESM record export,
module linkage, runtime behavior for every method, or combined peak memory of
all future workers. The complete goal remains open. Dynamic `ToPropertyKey`
effects, private names/brands, sendable class identity, historical execution
profiles, and many global/branch/exception preludes remain refusal paths.
