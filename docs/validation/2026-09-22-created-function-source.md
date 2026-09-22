# Created Function Source Validation

WriteCreatedFunction joins decoded creation, caller-regional target lookup,
runtime ASCII name parsing, call-type annotations, frame layout, file-backed
body decoding and executable base-function expression emission. Missing or
invalid metadata remains typed failure; no failed read becomes a default ABI.

## Evidence

- Initial typed-refusal stub failed the positive binary-backed fixture assertion;
  the implementation subsequently passed.
- Debug full build and regression: 81/81, 37.65 seconds.
- After review, strengthened tests for a valid explicit HaveThis-only annotation
  (three parameters instead of the default three hidden slots + one parameter)
  and exact/one-byte-short retained-body-plus-source scratch; focused Debug
  C++/Node tests 2/2, 0.06 seconds.
- Final ASan/UBSan C++/Node created/base function tests: 4/4, 0.30 seconds.
- Node executes the generated decoded-target functions and checks name, length,
  parameter identity, missing arguments, hidden-slot differences and ordinary
  construction return behavior. These are binary-backed fixtures, not complete
  application ABCs and not Ark runtime differential execution.
- Refusal tests cover missing caller/target/code, malformed annotation, invalid
  regional slot, unsupported opcode, mismatched code prefix, frame underflow,
  invalid/decorated/empty/non-ASCII names, mismatched UTF-16 length and budgets.
- Name prefix scaling: 200, 201, 4096 and 65536 characters.
- Independent review found no concrete production correctness issue and
  suggested the two added integration regressions. Reviewer inspected pinned
  runtime/name/ownership/budget behavior but did not independently run tests.
- `prlimit --as=1000000000 -- /usr/bin/time -v build/decode/created_function_test`
  exited 0, reported 0.00 seconds elapsed and 6,528 KiB peak RSS (6,684,672
  bytes), no children. This is not a full-pipeline memory proof.

## Limits

No record publication, recursive capture reconstruction, default parallel/lazy
task service or full decompilation CLI is added. No corpus-ready count increase
is claimed. The prior full-corpus probe was not changed or rerun. Existing
container profiles still require separate historical execution evidence before
the pinned runtime semantics can be asserted equivalent. Input ABCs and cached
static classifications are unchanged.
