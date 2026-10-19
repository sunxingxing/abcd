# Cross-plan lexical stores

`cells.js` is an independent source oracle. The engine receives only `cells.abc`.
The pinned compiler identity, SHA-256, options and input hashes are recorded in
`manifest.json`. Rebuild from this directory:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 --output cells.abc cells.js
```

`make` hoists a writer before the outer declarations. The writer directly
accesses `state`, but its descendant additionally demands the earlier `extra`
cell. Its final demanded cell plan therefore numbers the store differently
from the original access-only plan. `permuted` changes declaration/discovery
order and forwards two otherwise unused cells. `early` invokes the writer
before the outer declarations: RHS effects occur, then the original guarded
store must throw ReferenceError; a throwing RHS keeps its own exception.

The test exports the whole record with the production engine, checks Node
syntax, and compares 42 ordinary-source/engine scenarios: live reads and
updates, inserted/permuted descendant requirements, initialization/RHS throws,
TDZ ordering and distinct activation cells. A wrong-store mutation must change
observable results. At d233bd54 this fixture refuses FamilyWriteError::kStore
(method 648, PC37); it must now complete without changing the printed binding
plan or dropping a guard.
