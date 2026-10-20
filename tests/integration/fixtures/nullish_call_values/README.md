# Shared-phi nullish Reference fixtures

`manifest.json` pins the compiler identity, exact commands and source/ABC hashes.
The engine receives only ABC. Source files are independent Node oracles.

- `optionalArgument`: saved member callee, optional method with an argument,
  then a retained-value nullish selection.
- `nestedNullish`: two saved member callees surrounding the same argument
  selection; the inner result is an eager operand of the outer call.

O0 inputs publish; default-optimized inputs remain typed Reference refusals.
`nullish_call_values_source_test` checks those boundaries and differentially
checks both published fixtures plus the three existing `joint_selection`
optional/nullish fixtures. See cross-region-call-values.md for evidence.
