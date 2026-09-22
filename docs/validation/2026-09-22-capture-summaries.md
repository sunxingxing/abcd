# Capture Summary Corpus Validation

Evidence: `tests/corpus/capture-summaries.json`. Implementation is the commit
introducing this report. These are per-method captured-slot requirements, not
runtime cells, generated source or recovered records.

Classification manifest SHA256:
`3026acad938cd8a14b38e2e6b22f8c57534900dce7cb9097943729de02fd0d81`.

```sh
cmake --build build/decode-release --target abc_creations -j2
/usr/bin/time -v build/decode-release/abc_creations tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code \
  > build/capture-summaries.json
jq 'del(.results)' build/capture-summaries.json > tests/corpus/capture-summaries.json
```

## Results

- 900 paths, 402 dynamic files, 498 cached static exclusions.
- 4,922,559 environment-accepted methods summarized, zero summary failures.
  The other 563,695 decoded methods have exception-region analysis refusals and
  contribute no summary requirements.
- 5,088,318 captured lexical access sites become 2,542,362 distinct method-local
  depth/slot requirements. Local environment bindings are excluded.
- Maximum successful per-method scratch-plus-result payload: 22,128 bytes.
  This does not include its caller's flow/body or allocator overhead.
- Whole-process peak RSS: 162,684,928 bytes (158,872 KiB from `/usr/bin/time`).
  Enforced address-space limit: 1,000,000,000 bytes; no child workers.
- Wall time: 83.20 seconds. Exit zero; no base file/decode/CFG/index failures.

Verified with jq: after removing the five new summary fields and RSS, all
summary fields exactly match `creation-bindings-wide.json`. Summary successes
plus failures equal creation-inspected methods. Access-site totals exactly match
the prior environment probe's captured bindings. Per-file unique-slot and
access-site totals sum to the global report, and unique slots do not exceed
accesses.

The file with most captured requirements,
`4655acad3603c111523c7c96488f06bf.abc`, has 550,587 methods, 243,915 unique
method-local captured pairs and 398,597 accesses. This supports sizing a compact
per-file requirement store, but is not proof that eagerly retaining every
method analysis or closure context will fit the process budget.

## Paired Files

| ABC | Unique Slots | Access Sites |
| --- | ---: | ---: |
| Immersive/modules.abc | 208 | 382 |
| MultiCommunityApplication/modules.abc | 384 | 689 |
| pasteboard/modules.abc | 61 | 127 |
| push-kit clientdemo modules.abc | 42 | 74 |
| push-kit clientdemo widgets.abc | 6 | 8 |

No paired source text was read or copied into engine outputs.

## Verification and Remaining Work

Full Debug CTest passed 59/59; the new summary unit test passed ASan/UBSan.
Independent read-only review found no correctness, complexity or accounting
defects. Its CLI duplicate-counting gap was filled with an ABC fixture that
reads two captured slots at three sites. Move-assignment coverage was also
added and rerun.

The tool builds and releases one summary at a time. It does not yet publish a
bounded cache or query cross-function mappings across the whole corpus. Cache
identity/lifetime, lazy asynchronous execution and creation-context handling
remain required. Summary membership also does not prove value/TDZ behavior,
exception/sendable/class semantics or source equivalence. Full record recovery
and full-decompiler memory verification remain outstanding.
