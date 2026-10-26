# Lexical environment renewal (O0)

The manifest pins the compiler/disassembler identities and each source/ABC hash.
Commands use the same `--module --merge-abc --extension=js --record-name=...`
`--source-file=... --output=... --opt-level=0` form as object_methods.

- `renewal.js`: body-reset cells, multiple captured slots, old-cell mutation,
  calls during the next initializer, shared object identity and primitive
  payloads, nested fresh-entry environments, continue/break/throw, await and
  rejection. Closures run both synchronously and after suspension/loop exit.
- `finally.js`: ordinary finally with captured body cells. The existing
  guarded catch-payload proof closes cleanup rethrow domains on the new route;
  no source finally semantics are assumed from syntax or a local opcode shape.
- `fresh.js`: for-of/for-in/while captures retain their existing source route.
  Its output is also compared byte-for-byte with the baseline engine.
- `header.js`: captured header-copy binding remains a deliberate typed
  `kEnvironment` / `kAmbiguousJoin` / `kSourceScope` refusal.

`tests/integration/environment_renewal.mjs` runs 330 Node comparisons of original
and emitted modules. It compares return/throw identity, old closure writes,
ordering logs and suspension/rejection behavior. User-error messages are
compared; VM errors are compared by constructor name.

Owned semantic-body/certificate mutations and resource/work negatives live in
`tests/unit/analysis/environment_renewal_test.cc`. The geometric scale producer
compiles temporary real JS methods in a temporary directory; it never modifies
these fixtures or corpus ABCs. Wide slot/environment formats are exercised at
400 and 800 captured cells. Neither source text nor paired evaluation source is
passed to the decompiler.
