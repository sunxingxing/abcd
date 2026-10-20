# Guarded reads through pure expression transports

Engine input is ABC only; `transport.js` is an independent Node/compile oracle.
Pinned DevEco compiler SHA-256:
`4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

```sh
ES2ABC=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
"$ES2ABC" --module --merge-abc --opt-level=0 --record-name=transport --source-file=transport.js --output=tests/integration/fixtures/guarded_reference_transports/transport.abc tests/integration/fixtures/guarded_reference_transports/transport.js
"$ES2ABC" --module --merge-abc --record-name=transport --source-file=transport.js --output=tests/integration/fixtures/guarded_reference_transports/transport-default.abc tests/integration/fixtures/guarded_reference_transports/transport.js
```

- O0 publishes on both contracts. `run` transports a throwing conditional/add
  through nonthrowing `typeof`; `pure` transports a guarded captured binding
  through `typeof`. Calling `pure` before the declaration must throw ReferenceError,
  including under `typeof`. The real callee is saved before either argument.
- Default optimization produces the independently refused handler layout
  `PhasedMethodError::kRegion > ExceptionPlanError::kLayout >
  CatchRegionError::kHandlerShape` (numeric chain 5/2/1). This is asserted on
  both contracts; it is not a Node differential pass.
- 26 independent-process Node comparisons cover throwing lookup, branch getter,
  coercion and invocation, BigInt conversion failure, null/undefined receivers,
  argument-side callee/receiver replacement, and initialized null/undefined captures.
  Eight observed mutations detach `this`, remove the TDZ read, change the selected
  getter, or prematurely initialize the binding. Node checks emitted syntax.

`grouping_bytecode.py` is also run on original vs engine output, default and O0.
For the compile-only comparison, declaration exports on the original are spelled
as equivalent explicit string export aliases, so its existing identifier-alias
adapter applies identically to both files. No method-body or CFG normalization.
Strict whole-record result: **0 exact / 2 differ / 0 unverified**. Per method:
`run` is byte-for-byte instruction/handler identical in both modes; `pure` differs
only in `throw.undefinedifholewithname`'s diagnostic name (`provider` vs `$local0`).
Module entry adds a Hole check of `pure`: the recovered let binding is initialized
by its unchanged definefunc/store before this read, so that extra exception is
unreachable. Default mode has two associated register transport instructions.
Literal-table emission order and string/metadata byte offsets also differ; the
resolved literal meanings in each method and the module import/export identities
are unchanged. These are explained differences, never normalized passes. The
source token checker correctly rejects this as more than grouping cleanup.

SHA-256 (generated ABCs):

```
604555306b3508ba26e0dbbb8fa496537a79389b9d6280c793e5e58771d13c28  transport.abc
6631a222e6b16fb39c947100cd4996721b96467bdff8b10b7388e93a48dc6df2  transport-default.abc
```
