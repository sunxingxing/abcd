# Direct Reference Validation

Implementation: `7a94c33`. Command:

```sh
/usr/bin/time -v build/decode-release/abc_references tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code
```

| Metric | Observed |
| --- | ---: |
| Input files | 900 |
| Dynamic files | 402 |
| Hash-verified cached static exclusions | 498 |
| Decoded / flow / dispatch / reference methods | 5,486,254 each |
| Reference operands | 63,161,292 |
| Unique role/slot pairs, summed per method | 44,511,864 |
| Direct string payload reads | 38,356,493 |
| Direct literal-array payload reads | 2,423,270 |
| Literal entries read | 17,802,587 |
| Failed files / methods | 0 / 0 |
| Peak RSS, bytes | 150,278,144 |
| Address-space limit, bytes | 1,000,000,000 |
| Wall time, seconds | 207.02 |
| Exit status | 0 |

Debug and Release CTest each passed 24/24. The new integration fixture contains
a string operand and array-literal operand; the failure fixture uses an
out-of-range pool index. Both reuse classification and check static exclusion.
Existing tests continue to cover file confinement, lower inherited memory
limits and earlier-stage failures. No production-library implementation changed
in this integration; its preceding ASan/UBSan result was 18/18, not rerun here.

`tests/corpus/direct-references.json` retains per-file results, implementation
revision, reference tag and classification-manifest hash. File-result totals
were checked against the report's aggregate reference/string/literal counts.
Counts are per-method work, not globally unique entities: the same target may
be read repeatedly from different methods or slots. No corpus source or ABC
file was modified; paired source is not an engine input.

## Limits

The stage resolves all supported reference operands and parses directly
referenced strings/literal arrays. Method targets are range-checked only, not
matched to the method catalog. Nested literal references are not recursively
loaded; opcode-specific literal interpretation, module bindings and semantic
effects remain unfinished. This evidence is not source equivalence, historical
runtime certification, or completed per-record JS/TS/ArkTS decompilation.

Memory and time are local measurements of this validation pipeline. The future
parallel decompiler must independently satisfy the same whole-process ceiling.
