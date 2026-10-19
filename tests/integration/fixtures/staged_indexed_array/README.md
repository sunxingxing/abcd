# Staged indexed-array selections

`arrays.js` is an independent oracle. The engine receives only `arrays.abc`.
The pinned dynamic frontend (`manifest.json` records its SHA-256) was invoked as:

```sh
cd tests/integration/fixtures/staged_indexed_array
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 arrays.js --output arrays.abc
```

All three functions create an empty array, select its element value through a
conditional, and execute STOWNBYINDEX before passing the array through a private
object to a member call. `strings` has a three-way string choice; `values` includes
a computed **source read** (the array's own definition index is constant);
`several` has successive selected elements. Each defined index has the ordinary
writable/enumerable/configurable data descriptor. The interpreter's UseOwn /
DefineOwnProperty path does not invoke inherited setters. Native array literal
syntax has the same property-creation semantics; source indexed assignment does
not.

The Node differential compares 234 complete values/effect traces against the
original source. It exercises callee lookup before argument effects, exact
receiver identity, callee replacement during argument evaluation, conditional
short-circuit, throws at each source read/call, descriptor flags, private fresh
identities on repeated calls, and a hostile Array.prototype[0] setter. The runner
uses a scalar trace while that setter is active so harness array operations do
not contaminate the result. A mutation that substitutes indexed Set must fail.
Synthetic tests independently cover renamed registers, copies, a split between
allocation and selection, commit/cursor/coverage mutations, early exposure,
unfinished-activation backedges and exact byte/work budget boundaries at
200/201/400/800 elements. No source or sample identity enters production proof.
