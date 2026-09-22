# Record Structure Export

## Scope

Structure-first checkpoint: safe per-record paths, fresh-tree publication,
module declaration views, raw record fields, method names/kinds and ABI slot
evidence. This is not executable record decompilation. Class fields, globals,
local scopes, source signatures and bodies remain explicitly unresolved.

## Verification

- Debug warnings-as-errors build succeeded; full CTest: 86/86, 27.81 s.
- ASan/UBSan: record_path_test, module_writer_test, metadata_index_test: 3/3.
- Module regression observed failing before the fix. Covers regular namespace
  marker versus literal indirect export, consecutive local-export slot groups,
  import/export binding separation, duplicate import names, budget exhaustion,
  output quota and 200/201/8192-entry cases.
- CLI covers fresh publication, existing-target refusal, static-input refusal,
  invalid UTF-8 report path refusal before publication and staging cleanup.
- Independent review found three issues above; fixed and re-reviewed.
- `git diff --check` clean.

## Paired ABCs

Generated under `build/structure-paired-v2/`, without reading sidecar source.

| Input | Records | Methods | Peak RSS (KiB) | Seconds |
| --- | ---: | ---: | ---: | ---: |
| Immersive/modules.abc | 45 | 644 | 8448 | 0.04 |
| MultiCommunityApplication/modules.abc | 54 | 936 | 8448 | 0.06 |
| pasteboard/modules.abc | 19 | 219 | 8192 | 0.01 |
| push-kit-sample-code-clientdemo-arkts/modules.abc | 24 | 281 | 8448 | 0.02 |
| push-kit-sample-code-clientdemo-arkts/widgets.abc | 10 | 31 | 8320 | 0.00 |

External `/usr/bin/time` measurements; CLI enforces RLIMIT_AS 1,000,000,000
bytes. These small samples do not prove full-corpus/final-engine memory use.
Totals: 152 records, 2111 methods, 626 metadata fields, 241 module requests,
276 imports, 139 exports. Metadata fields are not source class fields.

`tests/integration/structure_syntax.cjs` verified all manifests, metadata JSON,
unique relative paths/counts and 152 declaration parses using TypeScript 5.9.3.
This is syntax validation only, not external-module resolution/type checking.
Validation-only package installed in `build/ts-parser`; npm dist integrity:
`sha512-jl1vZzPDinLr9eUt3J/t7V6FgNEw9QjvBPdysz9KfQDD41fQrC2Y4vKQdiaUpFT4bXlb1RHhLpp8wtm6M5TgSw==`.

The batch corpus was not re-exported in this checkpoint; its static
classification manifest was not changed. Async/parallel/lazy function tasks
remain a separate requirement; this synchronous structural CLI does not
implement that service.
