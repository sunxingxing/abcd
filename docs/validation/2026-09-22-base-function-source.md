# Base Function Source Validation

## Change

WriteBaseFunction emits an executable strict named function expression for the
ordinary non-sendable BASE_CONSTRUCTOR DEFINEFUNC subset. It supplies private
mutable frame bindings to WriteLocalLexicalBody, including initialized locals,
hidden self/new.target/this and declared arguments. It checks frame canonicality,
creation kind, target kind, name safety and encoded length equality.

This is a synchronous source primitive with caller-proven matching metadata and
runtime-parsed name. It is not a function discovery service, captured closure
reconstructor, historical execution guarantee, record emitter or decompiler CLI.
No corpus-ready counts or successfully published record counts have increased.
The existing full-corpus evidence remains source-local-lexicals.json; this
increment does not change that probe and did not rerun the full corpus.

## Executed Checks

- TDD: the initial implementation returned kCreation; the new positive test
  failed at its success assertion before implementation, then passed.
- Debug full build and CTest: 79/79, 38.96 seconds.
- ASan + UBSan: base_function and local_lexical_body C++/Node tests, 4/4,
  7.49 seconds. No sanitizer diagnostics.
- After review, added constructor primitive-return and newTarget.prototype
  getter count/exception tests; focused Debug 2/2 (0.08 seconds), sanitizer
  2/2 (0.19 seconds) passed with the strengthened Node oracle.
- Node executes generated function expressions for calls and construction,
  strict primitive/undefined receivers, replacement new.target, constructor
  return behavior, self identity, independent mutable slots, missing/extra
  arguments, local initialization, lexical cells and thrown object identity.
- Node checks fresh function/prototype identity, name, length, property
  descriptors, prototype.constructor and strict caller/arguments restrictions.
  Lengths 0, 1 and 255 are covered.
- C++ tests all 16 call-flag combinations; malformed frame fields; other
  function kinds, method creations, sendable, invalid names, length mismatch;
  Hole, captured/uninitialized lexical reads and unknown numeric results;
  exact/short output, bounded scratch, failing/throwing streams and register
  counts 200, 201, 4096, 65536.
- Isolated command:
  `prlimit --as=1000000000 -- /usr/bin/time -v build/decode/base_function_test`
  exited 0 in 0.03 seconds; peak RSS 8,316 KiB (8,515,584 bytes), no children.
- One worktree; no input ABCs or classification cache changed.
- Independent review found no concrete ABI/function-semantic mismatch, but
  identified an overstrong untouched-output contract: actual allocator failure
  during the second body preparation can leave a wrapper prefix. The contract
  now distinguishes deterministic configured-budget preflight from allocation
  failure and requires atomic publication. The reviewer inspected runtime code
  and tests but did not independently execute the test suite.

These are source execution tests in Node and pinned runtime code inspection,
not differential execution against Ark. Full-process concurrent admission,
atomic record publication and application-level equivalence remain unproven.
