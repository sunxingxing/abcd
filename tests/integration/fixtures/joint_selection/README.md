# Joint selected-value ownership fixtures

`manifest.json` records the pinned DevEco es2abc SHA-256, exact commands,
source hashes and ABC hashes. Sources are Node oracles, never engine inputs.
Each export is compiled separately so a refused member cannot hide the measured
recovery status of another fixture. Both O0 and default ABCs are retained.

O0 booleanJoin, booleanNot and booleanConjunction publish. The integration test
compares 21,000 engine-output/original scenarios (null/undefined, falsy/truthy
values, getter throws, noncallable timing, replacement of the saved method and
receiver identity). Receiver and logical-operator mutations are observed.
Strict SDK recompile/disassembly comparison is identical for all three emitted
fixtures at both O0 and default optimization, including their module entry.

The optimized-input variants retain precise CallReferenceError::kNotInline
assertions. O0 optionalMethod, optionalProperty and nullishChain now publish
through the independently checked shared-phi truth view. Their full Node
matrix is registered as nullish_call_values_source_test, including nested
saved member calls and throwing/receiver/short-circuit cases. See
cross-region-call-values.md for the exact contract and compiler comparisons.
