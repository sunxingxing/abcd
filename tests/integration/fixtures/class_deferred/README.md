# External class-root deferred capture fixtures

Real dynamic ABC compiled from the adjacent independent ES module oracles with
DevEco Studio es2abc (ABC 24.0.0.0). The sources are used only by the Node test,
never as decompiler inputs. From the repository root, replace NAME:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc \
  --module --merge-abc --opt-level=0 --record-name=NAME --source-file=NAME.js \
  --output tests/integration/fixtures/class_deferred/NAME.abc \
  tests/integration/fixtures/class_deferred/NAME.js
```

`abc_semantic FILE METHOD --instructions` confirms the actual lowering:

| Fixture | Creator | Environment allocation / class creation / cell store PCs | Reading member / load / guard PCs |
|---|---:|---|---|
| self | 510 | 13 / 18 / 33 | 455 / 6 / 8 |
| late | 494 | 0 / 18 / 93 | 467 / 6 / 8 |
| multiple | 529 | 0 / 18 and 46 / 148 | 447 and 474 / 6 / 8 |
| heritage | 454 | 13 / 27 / 42 | 427 / 6 / 8 |
| reentrant | 482 | 13 / 18 / 33 | 428 / 6 / 8 |

These are evidence, not production recognition patterns. `self` also forwards the
same demand through an arrow. `multiple` has distinct creation/request identities
for the same outer storage. `late` invokes the member before and during the
initializer, and the runner retains the member after a throwing initializer.
`phased` exercises a member with its own catch: the external TDZ state must
reach the checked-read projection without erasing its throwing event.
`heritage` checks superclass evaluation/throws without merging the two class
names. `reentrant` calls the member while constructing an instance.

`class_deferred_cli_test` runs both numeric+constructors and language-source
contracts. It compares values and control/log traces, including `ReferenceError`,
`RangeError` and `SyntaxError` constructors, not messages or stacks. It is a Node
source-language oracle, not an Ark bytecode execution claim. Hoisting the outer
let declaration and replacing the class self alias must change the result.

`static_early` and `loop` pin explicit first-increment refusal boundaries:
static-field timing is still a record-program class failure, and a class inside
a loop in a nested function is still a family occurrence failure. They do not
claim positive activation recovery. The IR tests separately reject forged
activation, owner/revision, storage target, store, declaration kind and scope;
exercise copies/block splits and sizes 200/201/400/800; and check exact byte/work
boundaries. The real-file test independently rebuilds root capture obligations,
including rejecting deletion of a deferred request or substitution of its target.

SHA-256:

```
d9501b420b0a593a9fd6ba3bf296d1e1e5594caf9e545edaf928f93e454f511e  heritage.abc
432ee8f54bb44578d1ef8a5c71c89666aba903f7f9910464bd28d6ca4345aa78  late.abc
8a2bca7ed206eb1affe5734fb5e9fbf5e3dcc0a6e99706d5ba59065d2ee7a61d  loop.abc
b15f6addb3cf48f110a4e35d2b07acaca8cde7795d20caa396065b0d5190a6fa  multiple.abc
6e76a2633c8150e985f179c257f6e9aebb5d5169a9bb530fc43aa5eda8e5dd57  phased.abc
bac74421a021301806f51cd150e3a776739f2a4981f0c9ca071cf4154a7540dd  reentrant.abc
34dd3b38d3831d066fb92692463cb4982d8538d6fb8eeba4f8fa8d91da76934f  self.abc
8a4716890a290b7414f0d448a5ee54ccec4cfa9de95f9a8d52b46ac68c95ea9f  static_early.abc
```
