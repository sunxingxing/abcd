# Dynamic Record Index Verification

Scope: dynamic class/record inventory, inline member traversal, method ownership,
method names and lazy code locations. **No instructions, exception tables or
function semantics have been decoded. No decompiled source has been produced.**

## Reproduction

Implementation: `705a212`, `3f51d0d`, formatted at `878c5cd`. CLI integration and
root-confined file opening: `95629a7`, `2c8117f`.
Additional malformed-input test coverage: `47f8b26` (production unchanged).

```sh
cmake -S . -B build/index-release -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_CXX_FLAGS=-Wall -Wextra -Werror'
cmake --build build/index-release -j 4
ctest --test-dir build/index-release --output-on-failure
/usr/bin/time -v build/index-release/abc_index tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code
```

Persisted per-file report: `tests/corpus/record-index.json`. Its validation
section identifies the implementation commit and input manifest SHA-256.
Counts are per file path, including duplicate contents, not unique functions.

| Measure | Result |
|---|---:|
| Files rehashed | 900 |
| Dynamic files indexed | 402 |
| Static files skipped using cached evidence | 498 |
| Failed files | 0 |
| Local records | 281,067 |
| Methods with code locations | 5,486,254 |
| Instruction bytes located, not decoded | 712,869,853 |
| Peak RSS, CLI and GNU time | 151,175,168 bytes / 147,632 KiB |
| Wall time, warm filesystem cache | 17.61 s |
| Exit status | 0 |

The process ceiling remained 1,000,000,000 bytes. The sequential driver reserves
256 MiB for each index; only one file index is retained at a time. No child
processes are launched by the indexer. This is **not** a memory measurement of
the eventual concurrent source decompiler.

| Paired sample | Records | Methods |
|---|---:|---:|
| Immersive/modules.abc | 45 | 644 |
| MultiCommunityApplication/modules.abc | 54 | 936 |
| pasteboard/modules.abc | 19 | 219 |
| push-kit-sample-code-clientdemo-arkts/modules.abc | 24 | 281 |
| push-kit-sample-code-clientdemo-arkts/widgets.abc | 10 | 31 |

## Findings Resolved

- Initial parsing rejected 399 files because absent field/proto tables use paired
  reserved words. The pinned writer confirms this version-12+ convention; mixed
  sentinels and old-profile omissions remain errors.
- The default 64 MiB per-index allowance rejected the three largest indexes.
  The sequential CLI now admits 256 MiB without raising the 1G process ceiling.
- Review caught string-capacity/transient-allocation accounting and tag-order
  gaps. Regression tests now cover these alongside permitted annotation repeats.
- Pinned UTF conversion code emits four-byte sequences for surrogate pairs.
  Tests cover those, six-byte surrogate pairs, encoded NUL and invalid encodings.
- A directory-symlink regression demonstrated reads outside a selected root.
  `OpenBeneath` now traverses held directory descriptors with `O_NOFOLLOW` at
  every relative component; parent and absolute paths are rejected.
- A test fixture unlinked an open file before parsing, changing ctime and
  intermittently triggering `kFileChanged`. Cleanup now occurs after parsing;
  the implementer verified 50 repeated focused runs.

## Gates and Limits

- Debug: 9/9 CTests passed with warnings treated as errors.
- Release: 9/9 CTests passed with warnings treated as errors.
- ASan/UBSan: 7/7 library CTests passed. CLI excluded because ASan shadow
  reservations conflict with the ordinary address-space cap.
- Tests include 200/201 region and record cases, regional owner/proto resolution,
  foreign skipping, duplicate class offsets, malformed strings/LEB/tags/code
  bounds, budget exhaustion, stale content/rules, inherited lower memory limits
  and directory symlink traversal.
- Independent specification and quality reviews completed; identified findings
  were fixed and re-reviewed.
- Sorting and binary region lookup avoid the upstream per-reference linear
  region search. Costs scale with table sorting, metadata traversed and names
  materialized; this is not a benchmark of instruction or semantic recovery.

All observed dynamic versions (9.0.0.0, 11.0.2.0, 12.0.2.0, 12.0.6.0 and
13.0.1.0) passed this metadata stage. This does not certify their ISA or semantic
profiles. Next work is typed instruction/operand and exception decoding, bounded
asynchronous method tasks, semantic recovery, one JS/TS/ArkTS file per record,
and behavioral validation. The full user objective remains unfinished.
