# Merged Module Validation

Implementation: `42be4c3`. Command:

```sh
/usr/bin/time -v build/decode-release/abc_modules tests/corpus/abc-index.json \
  batch /home/rabbit/sample/abc paired /home/rabbit/sample/abc_with_code
```

| Metric | Observed |
| --- | ---: |
| Input files | 900 |
| Merged dynamic files | 289 |
| Unsupported bundle dynamic files | 113 |
| Cached static exclusions | 498 |
| Checked published records | 275,891 |
| Checked module payloads | 274,355 |
| Module requests | 1,316,276 |
| Import entries | 1,675,370 |
| Export entries | 652,943 |
| String entity reads | 5,946,130 |
| Peak RSS, bytes | 149,704,704 |
| Address-space ceiling, bytes | 1,000,000,000 |
| Wall time, seconds | 36.13 |
| Exit status | 1 |

All 113 diagnostics are explicit unsupported-bundle failures. No merged
attribute, module-table or string validation failures occurred. This is not a
full-corpus module pass: bundle support is still required. The counter named
failed_module_records includes one file-level failure for each unsupported
bundle; it is not a count of discovered bundle module records.

The tool follows merged-record publication by CommonJS/JSON field presence.
Records outside that runtime catalog do not count as checked. Strings are
validated one at a time, including request specifiers and import/export names.
String reads may repeat across entries; counts are not unique identities.
JSON content is checked only as a string, not parsed as JSON. Request-phase
payloads, package/npm metadata, binding resolution and behavior are not checked.

Debug and Release CTest each passed 27/27. The module CLI fixture verifies
merged discovery, an empty module table, static exclusion and nonzero failure
for an unsupported bundle. No production library changed in this integration;
the previous library ASan/UBSan result was 20/20, not rerun here.

tests/corpus/merged-modules.json retains all per-file diagnostics and counts,
the implementation identity and input manifest hash. Per-file module, record
and string totals agree with the aggregates; all failure categories were
checked. Input ABCs and paired source were not modified. This measurement does
not certify historical semantics, full decompilation or per-record source output.

The next implementation requirement is bundle module discovery using the
pinned unmerged special-class rules, followed by revalidation of all 402
dynamic files. Do not hide these 113 failures by treating bundle files as empty
modules or by excluding them from the dynamic corpus.
