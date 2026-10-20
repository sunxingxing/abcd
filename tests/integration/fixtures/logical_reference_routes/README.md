# Joint normal selection/reference fixtures

`manifest.json` pins the actual DevEco es2abc identity, commands and input hashes.
`calls.abc` is O0; `default.abc` is the default optimization. The three exports
exercise two optional selections with independent child-call snapshots, ordinary
conditional arms, and earlier argument reads before both optional results.
`retained.abc` deliberately retains the existing `kNotInline` refusal: this
round does not discover earlier retained predicate/keep-edge selections through
an uncommitted cross-region expression. JS files are independent Node oracles;
only the ABCs enter the engine.

The Node test compares 1,728 scenarios per compiler mode, including getters,
throws, non-callable timing, null/undefined, receiver identity, callee replacement,
coercion and function `.length`/`.name`. A value-call mutation is observed.
The design note records the strict default disassembly equality and the explicit
O0 private-snapshot differences, using the grouping acceptance helper.
