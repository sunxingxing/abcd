# Member-call reference fixtures

ES modules compiled once to dynamic ABC for `call_references.mjs`. Provenance:
DevEco Studio's bundled es2abc (`--bc-version` 24.0.0.0), command
`es2abc --module --merge-abc --record-name=NAME --source-file=NAME.js --output NAME.abc NAME.js`.

| Fixture | What it pins | Expected |
|---|---|---|
| `single` | `o.f(G(x.g?.h))` alone in its statement | complete, Node-equal trace |
| `twoargs` | An eager argument before the selection argument | complete, Node-equal trace |
| `computed` | A computed key with a side effect | complete, Node-equal trace |
| `rewrite` | The argument replaces `obj.f`; the old function is still called | complete, Node-equal trace |
| `throwing` | The argument getter throws a sentinel; the call is not made | complete, Node-equal trace |
| `noncallable` | A non-callable callee throws only after the arguments ran | complete, Node-equal trace |
| `optarg` | `o.f(G(x.g?.h))`: the member callee is read before an argument whose optional chain splits the call's region | refused (its nested `??` selection has no single consumer) with `FunctionTreeError::kExpression → ExpressionError::kUnsupportedOperation → CallPlanError::kReference` |

`harness.mjs` defines `globalThis.obj`, `x` and `G` with logging getters so the
evaluation order (`get f` before the argument's getters) and the receiver are
observable once the call is admitted.

```
d557962cde46d5c6cc9f287c9f11cfa062668c17ca833c728f865230c11f78c9  optarg.abc
c2d8abb9e3711a396d54724ceb685045698e327f6029c097fefd3954065df4d1  single.abc
8b5c69f80c09194d7d6b48252f49be57f619056a70bbaf5575f4f1a95dbdcaf9  twoargs.abc
03112abf3dee4c6d64708011b87e29f7db153cd211d9d184d72e8427d3e96a7d  computed.abc
9df10cf65f28201f99e265f220b894b522e8afe61be6bfbc3d168e2aaeb0066f  rewrite.abc
fcd38ba0f42ec288ffe8da75c9a041215b2524580c902c218f27d4b0cfb72f89  throwing.abc
fd7fc15f25310094b7a0ff9079bba7adb0edfec1089065722377e973e2a41f72  noncallable.abc
```
