# Private argument-array call spread

The fixture is compiled with the pinned DevEco dynamic es2abc. Compiler SHA-256,
source/ABC SHA-256 and options are recorded in `manifest.json` and checked by the
integration test. Command, from the repository root:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 --output=tests/integration/fixtures/call_spread/calls.abc tests/integration/fixtures/call_spread/calls.js
```

`call_spread_source_test` exports the ABC through the production engine, checks
Node syntax, and compares engine output with the source oracle. Cases include
single/mixed/two calls, empty/sparse/string inputs, saved member identity while an
argument rewrites that member, receiver identity, non-callable callee timing,
and throws from callee/argument/iterator/next/done/value/last evaluation. A caught
call exception exercises the phased route. The original flags refuse kPremise.

The same test recompiles the engine output with the pinned compiler and the
options above, comparing each method's ordered createemptyarray/STARRAYSPREAD/
APPLY sequence (including the two APPLY sites in `twice`) and the raw APPLY
argument form. **Compatibility adaptation:** this es2abc cannot parse the existing
printer's string-named exports (`export { x as "x" }`). Only identifier export
aliases are unquoted in the test's temporary recompile input; bodies are copied
unchanged. Thus unmodified full-file round-trip does not pass; the adapted
call-body round-trip does. No production export formatting was changed.

Under the separately approved V5 premise, standard JS omits the second iterator
acquisition on the private hidden argument array. The test demonstrates this
observable difference with a throwing Array.prototype[Symbol.iterator] getter;
it is not reported as an Ark runtime equivalence pass. Node covers ordinary
inputs. No Ark interpreter execution is claimed.

Unit tests cover raw roles/effects/commit, proof and tree mutations, copies,
200/201/400/800 construction sizes and exact CallPlan work boundaries. The normal
cross-block scheduler still retains the array root: block-split cases verify
construction but retain typed kOrder. This is an implementation limitation, not
a language requirement.
