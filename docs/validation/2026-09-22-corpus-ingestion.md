# Corpus Ingestion Verification

Scope: header layouts, streaming fingerprints, classification, persistence and
reuse. **No method decoding or source decompilation has been performed.**

## Corpus

| Corpus | Files | Dynamic | Static | Unknown | Invalid |
|---|---:|---:|---:|---:|---:|
| `/home/rabbit/sample/abc` | 895 | 397 | 498 | 0 | 0 |
| `/home/rabbit/sample/abc_with_code` | 5 | 5 | 0 | 0 | 0 |
| Total | 900 | 402 | 498 | 0 | 0 |

Total bytes: 1,940,139,600. Unique SHA-256 contents: 894.
Largest file: 172,688,064 bytes. The scanner does not load entire files.
All 498 historical static files required the 72-byte candidate header, correct
Adler32 and a structurally parsed ETS class-language witness, not version alone.
Evidence is persisted in `tests/corpus/abc-index.json`, detector revision 1.

## Actual Runs

Command, from the project root:

```sh
/usr/bin/time -v build/ingestion/abc_scan tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code
```

| Run | Files hashed | Classifier calls | Reused | Peak RSS | Wall time |
|---|---:|---:|---:|---:|---:|
| Initial, Debug | 900 | 894 | 6 | 11,665,408 bytes | 3.94 s |
| Repeat, Debug | 900 | 0 | 900 | 12,447,744 bytes | 1.86 s |

Both exit statuses were zero. GNU time independently reported 11,392 and 12,156
KiB maximum RSS. The second run hashed the same 1,940,139,600 bytes while avoiding
every classification, including all static detections. Timing is a local
observation, not a complexity proof or performance promise.

CLI establishes a 1,000,000,000-byte address-space ceiling before scanning;
this is more restrictive than an RSS-only limit. No child processes are launched.
These measurements do not establish the memory cost of future IR or source recovery.

## Verification Gates

- Debug with `-Wall -Wextra -Werror`: 7/7 CTest executables passed.
- Release with the same warnings: 7/7 passed; checks do not depend on `assert`.
- ASan/UBSan library configuration: 6/6 passed. CLI omitted because ASan shadow
  address-space reservation is incompatible with the normal process ceiling.
- Tests cover reader transactions, dynamic/static header bounds, checksum and
  positive static evidence, probe budgets, read-only file lifetime/mutations,
  FIFO rejection, manifest schema consistency, lock conflicts, failed-write
  preservation, duplicate content, content/rule invalidation, root preservation,
  and directory-symlink suffixes.
- Spec and quality review findings were fixed using failing regressions before
  implementation changes. Final root-symlink regression was independently rerun
  by the quality reviewer.

Observed toolchain: GNU C++ 13.3.0, CMake 3.28.3, OpenSSL 3.0.13, zlib 1.3.
JSON dependency identity is pinned in `docs/reference/dependencies.md`.

## Remaining Acceptance Work

All 402 dynamic inputs still require record/method indexing, profile-specific
instruction decoding, bounded asynchronous method requests, precise exception/
effect/closure semantics and JS/TS/ArkTS generation. Each record needs its own
output file. Full output coverage, behavioral validation and complete-process
memory measurements remain unproven. Static classifications are exclusions,
not decompilation successes.
