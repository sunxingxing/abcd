# Guarded-capture (TDZ) fixtures

Small ES modules compiled once to dynamic ABC so that `guarded_captures.mjs` can
compare the engine's output against the original source under Node. The test does
not need a compiler. Each `.abc` file is the compiled form of the `.js` file with
the same name.

Provenance: DevEco Studio's bundled es2abc
(`sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc`,
`--bc-version` 24.0.0.0). Command:

```sh
es2abc --module --merge-abc --record-name=NAME --source-file=NAME.js --output NAME.abc NAME.js
```

| Fixture | What it pins | Expected |
|---|---|---|
| `probe` | Hoisted function reads a later `let`; a call runs before the store | complete, TDZ preserved |
| `twice` | Two cells initialized at different points between probes | complete, TDZ preserved |
| `mutate` | Deferred declaration followed by a later assignment | complete |
| `parentread` | Parent arrow reads the cell before initialization | complete, TDZ preserved |
| `rhs` | Closure invoked while the initializer is being evaluated | complete, TDZ preserved |
| `laterblock` | The cell's only store follows a branch, in a later block of the same scope | complete, TDZ preserved |
| `forward` | A hoisted function forwards a nested arrow's guarded read | complete, TDZ preserved |
| `forwardmixed` | Two nested arrows, both guarded, forwarded through one function | complete, TDZ preserved |
| `forwardwrite` | A nested arrow writes the cell; the forwarded obligation stays Language | refused: `ScopeError::kInitialization` |
| `childwrite` | Child writes the cell before initialization (the VM store has no TDZ check) | refused: `ScopeError::kInitialization` |
| `branch` | es2abc hoists the block environment to function entry; the store sits in a branch | refused: needs scope narrowing |

SHA-256 of the compiled inputs:

```
c0fbf13abf7fc5f3433776af085afa9b0a2c751860cf0b753a8c805c65dc9836  probe.abc
85fe49d1b1536edece76ba1aaf32e77bcac46801eff6f573a1607048023953b7  twice.abc
0274695244c73219b8074a362fba70f4319030405aeccaecc442c3f210c32827  mutate.abc
12071c2f462ecb85ab6f10459c88bb6f059a37ebd08ea01a11d71eb975b579f6  parentread.abc
24c3a72bb1fec3e44132ecebd3538539012a54503be407d6cf690b00afd1a019  rhs.abc
bd5bdfebd1ce7bf8a10ddb2bbd62891c0934efb6e8de85d052eaed6c08e190d1  childwrite.abc
6d703927cdbcf1a76b88ddcecc899b9950b1520e9b8f888e4b3ff81ebb326b46  branch.abc
072f367934f9562cddab018cfb6ac0ee1da00eb7713b6464d6c95f6fbabf7dd9  forward.abc
ccf64bb2fc9c19878425d9a69348f1fb9f63d5bb3499e0cba1a8c071e7623ac3  forwardmixed.abc
40a26d802a56e1bc81323dccc756dead73004b01c616c7afc2e2927cb4929dbd  forwardwrite.abc
763aec455a5ba9432ff7f521a20c54c5a727c01872f610cf1d73bfee3fa87b48  laterblock.abc
```
