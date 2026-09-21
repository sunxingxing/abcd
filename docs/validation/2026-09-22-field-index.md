# Field Index Validation

Implementation: `fc40f45`. Full-corpus command:

```sh
/usr/bin/time -v build/decode-release/abc_index tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code
```

All 402 dynamic files indexed successfully: 281,067 records, 5,486,254 methods
and 1,675,075 retained fields. The other 498 files were hash-verified cached
static exclusions. Exit status was 0, wall time 17.45 seconds and peak RSS
150,392,832 bytes under a 1,000,000,000-byte address-space ceiling. These are
local measurements of indexing, not of the future complete decompiler.

The reduced per-file evidence in tests/corpus/field-index.json preserves counts,
outcomes, implementation identity and classification-manifest hash. Summed field
counts match the aggregate. No sample input was changed or copied into output.

Debug and Release CTest each passed 24/24; ASan/UBSan passed 18/18. New assertions
cover retained field slices/ownership/names/type indexes, absent values,
signed-LEB zero/one/minimum/maximum/negative-one bits, raw word values and
200/201/1024-field records. Existing malformed LEB/tag/owner/range tests remain.
The string-capacity test's budget includes retained FieldInfo storage so it
continues exercising string growth rather than failing earlier on fields.

This establishes structural field retention only. Field-name classification,
type resolution, wide-value loading, module-request/import/export tables,
bindings and JS/TS/ArkTS record source recovery are not yet implemented.
