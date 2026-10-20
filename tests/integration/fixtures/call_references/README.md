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
| `phplain` | `globalThis.obj.f(globalThis.x.g.h)` inside try/catch (phased route) | complete, Node-equal trace |
| `phtrue` | `obj.f(H ? x.g.h : G(1))` inside try/catch, taken arm | complete, Node-equal trace |
| `phfalse` | Same shape, other arm | complete, Node-equal trace |
| `phthrow` | The selected arm's getter throws; caught, the call is not made | complete, Node-equal trace |
| `phrewrite` | The selected arm replaces `obj.f`; the old function is still called | complete, Node-equal trace |
| `phnoncall` | Non-callable callee throws `TypeError` after the selected argument ran; caught | complete, Node-equal trace |
| `realmlit` | Object/array initializers holding `globalThis` itself or reading from it, in normal and phased code (the closed realm value must stay a literal initializer) | complete, Node-equal result |
| `phconcat` | `obj.f('a' + (H ? x.g.h : G(6)) + 'b')` inside try/catch: callee composed across the selection | complete, Node-equal trace |
| `phtwo` | Two selections in one concatenation; the Binary composes across the second | complete, Node-equal trace |
| `phsiblings` | Two sibling selection arguments | complete, Node-equal trace |
| `phprim` | `P + (H ? G(4) : 0) + P` with a logging `Symbol.toPrimitive` | complete, Node-equal trace |
| `phprimthrow` | The Binary's `Symbol.toPrimitive` throws after the selection; caught | complete, Node-equal trace |
| `phrewritearm` | The selected arm replaces `obj.f` inside a concatenation | complete, Node-equal trace |
| `phassign` | `r = 'a' + S1 + 'b' + S2` (no call root) | complete, Node-equal trace |
| `phthrowarm` | The selected arm's getter throws inside a concatenation | complete, Node-equal trace |
| `phnested` | A selection nested in another selection's arm inside a concatenation | refused, same chain as `optarg` |
| `phorphan` | A pure `typeof` wrapper over a selection Binary beside another composed call | completes with the transitive original-throw summary; receiver/getter mutations and the Node oracle remain mandatory |
| `phcomma` | `obj.f('a' + (G(0), H ? x.g.h : 1) + 'b')`: a comma side effect is a root event inside the span | refused, same chain as `optarg` |
| `optarg` | `o.f(G(x.g?.h))`: the member callee is read before an argument whose optional chain splits the call's region | refused (its nested `??` selection has no single consumer) with `FunctionTreeError::kExpression → ExpressionError::kUnsupportedOperation → CallPlanError::kReference` |

The `ph*` and `realmlit` fixtures were compiled with `--opt-level=0` added: this es2abc's
default bytecode optimizer writes catch blocks with `code_size` 0, which the
current catch-region layout refuses (a separate gap, see status.md).

`harness.mjs` defines `globalThis.obj`, `x`, `G`, and `P`/`Q` (logging and throwing `Symbol.toPrimitive`) with logging getters so the
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
901690304bfe874a71e6d0615c6a71481a5b90bc0a810ccc027077698fef4878  phplain.abc
a03f0b66a60072aee69a04882af437f2f1e60bddaf3aff079c7adacdeb4cfb5b  phtrue.abc
159af13a89669aa6397f7f5422a737e6e36609f7a36d06b17e99f9a58d333d01  phfalse.abc
fe8e0804a541d8874493eb89e053551bc460ed6ad5433c5d6eb48f129bcd8920  phthrow.abc
8ce552ee678b3e61d90652aa90e151e198f76c8128d61071b8289aecd0e9b460  phrewrite.abc
5a2de38cbecf5d8f73b19a4506085be693c51f2cc8236545ec719e786b4c2ff4  phnoncall.abc
c4ea8bdb726d5f7b79d0cb7da68902e88c777a3e96a6663d4ddb8af1aef7a930  phconcat.abc
5927312aa0284bfe5daec9a776642d9d602f21c9c031e211445196cbd3a1e9a1  realmlit.abc
98954334ca12cec286cb881f52ab50602b8235c2dbc16c8e08c556b3dd18a765  phtwo.abc
160cfca6e50f14db30fdad7a488bd560960aed7321cc5ed8c5bd48eb549da948  phsiblings.abc
6819edb2e317c1575622fae5cca4bc779d53f83834eb94f38ff2773ea578044a  phprim.abc
3a0d5164128be1623391d5165db9d909cdb1bc454dd56edb7c82b77e23ecd262  phprimthrow.abc
2515b8b61232179d432e83e69cdfe5c0e89ab635e7150a42e9ec2059424bb658  phrewritearm.abc
b06ec12a00a23686fca81bda3de1bbf8eb39f5441e88aadc8e24516f7553cddd  phassign.abc
ef51949b0c8e20a27d3428d8a36ae31a9a2d6936d043cc004b071ad8d81abf0c  phthrowarm.abc
72305260de912a7634d863363b0a966417a39b7164ab9d7edc3371c3e0b606a3  phnested.abc
da94432a52211d17c8177aa2db0eeafe19b02bad608420363a505e4ad729104c  phcomma.abc
15a2fabc92e4114520fa3224a053c1442c8f398e742943d4371ed204c199952b  phorphan.abc
```
