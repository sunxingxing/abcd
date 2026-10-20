# Staged object fields under a saved member callee

`build.json` pins the compiler SHA-256, exact O0 commands and source/ABC hashes.
Only ABC reaches the decompiler. `fields.js` is the independent Node oracle.

- `run`: a saved getter/callee and receiver, then four ordered own definitions;
  guarded capture/member read; a retained constructor followed by two selected
  arguments; a final selected value.
- `conditional`: guarded member calls in both arms of a field selection, with
  getter-traced siblings before/after that field. Existing checked Reference
  proofs must close each arm before the outer literal is composed.
- `tdz`: separate negative fixture. Calling a closure before its captured `let`
  initializes remains refused at `FunctionTreeError::kOwnership >
  TreeObligation::kExpressionOwnership`. This is not counted as a published
  Node differential and is not admitted by the new field proof.

`literal_field_forests.mjs` compares engine output against the independent
source under both contracts (352 scenarios): callee/getter/constructor/branch
throws; receiver identity; replacement during field evaluation; property keys,
values and descriptors; inherited setters must not execute. Wrong field,
callee and selected-arm mutations must change the trace.

`staged_field_roundtrip.py` recompiles original and emitted sources at default
optimization and O0. It imports the assembly reader from grouping_bytecode.py,
retains all unnormalized per-method differences, and performs the documented
AST-based identifier export-alias adaptation independently on both sources.
Differences are evidence, never renamed/register-normalized into equality.
