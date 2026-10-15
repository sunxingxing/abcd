# Await catch-input original-flow fixture

Independently authored source, not paired application source. Pinned DevEco
`es2abc` SHA256:
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

From the repository root:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 --record-name=await_catch_flow \
  --source-file=flow.js --output=tests/integration/fixtures/await_catch_flow/flow.abc \
  tests/integration/fixtures/await_catch_flow/flow.js
```

`flow.abc` SHA256:
`488789a4b7851a2075b970e724aacc5946aec9a55683663263d09c7990495084`.

The CLI test checks the actual await protocol and exception edges, independently
verifies every raw catch-input query, and requires at least one method which the
old fast proof cannot prove (`kUnprovedPaths`). It then executes engine-exported
source under both contracts. Four functions x six modes x two contracts compare
values, exception constructors, receiver identity, ordinary effects and microtask
order. A dynamic value read before the await remains visible in the catch;
ignored catch bindings, nested catches, rethrow and benign finally are covered.
Erasing suspension and changing the preserved local are detected mutations.

No finally-body throw is used in this Node source oracle. es2abc can leave an
inlined finally inside a protected range; such throws must use an explicit
ABC-handler-derived oracle, never the original source's Node behaviour. The
production proof preserves every original dispatch edge and does not simplify
finally or authorize effectful IteratorClose. The read-only test probe assumes
this fixture's checked trailing reject wrapper; it supplies no production gate.
