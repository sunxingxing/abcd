# Own Property Write Evidence

This checkpoint is not completion of instance fields, globals, source signatures
or function-body integration. Receiver identity is explicitly unknown. No source
field declarations or moved initializers are emitted.

- Typed lift: modern narrow/wide STOWNBYNAME and DEFINEFIELDBYNAME; validates
  operand roles, widths and receiver range. Ordinary setter-capable assignments
  are excluded. Runtime reference is the pinned v7.0 interpreter handlers.
- Record output reuses one decoded method, adds a linear site scan and resolves
  one regional string at a time. Invalid pool indices/text stay site-local;
  input IO/change and output failures remain fatal.
- Debug tests: 92/92 passed, 28.18 seconds.
- ASan/UBSan: own_write_test and class_writer_test, 2/2 passed.
- Review: no blocking findings; requested bad-index/bad-name isolation regression
  added and passed, with a normal write after both invalid sites.

Five paired ABCs produced 152 records and 2,111 methods under
`build/structure-own-writes/`. TypeScript 5.9.3 declaration syntax, JSON schema
assertions, ownership and manifest counts passed. Detected 160 own stores and
1,997 field definitions, zero site failures. These are write sites, not recovered
instance-field counts. Prior 101 class sites and 374 member entries remain.

External peak RSS: Immersive 8,832 KiB; MultiCommunityApplication 9,216 KiB;
pasteboard 8,576 KiB; push modules 8,704 KiB; widgets 8,448 KiB. Each command uses
the CLI's 1,000,000,000-byte address-space limit. This small sequential sample
run does not certify a complete asynchronous decompilation pipeline.

Static classification cache and sample files were not modified.
