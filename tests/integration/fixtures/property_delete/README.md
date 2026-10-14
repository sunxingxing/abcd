# Round 50 property_delete

Sources in this directory are independent Node evaluation oracles, never engine inputs.
DELOBJPROP (0xc2); the phased instruction has a real handler. Strict delete checks values, exception constructors and full traces. Proxy get traps and accessors must not run; delete traps and ToPropertyKey do run. The unused array-return representation is kept as a typed-refusal regression, not an engine success.

Compiler: DevEco es2abc, SHA-256 `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

```sh
ES2ABC=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
"$ES2ABC" --module --merge-abc --opt-level=0 --record-name=normal --source-file=normal.js --output normal.abc normal.js
"$ES2ABC" --module --merge-abc --opt-level=0 --record-name=phased --source-file=phased.js --output phased.abc phased.js
```

The integration test asserts the target instruction and exceptional dispatch before comparing Node executions. Both flags are checked. Exception constructor and full observable traces are compared; message/stack text is not. There is no finally-throw source-oracle claim. The tests do not execute the pinned Ark VM.

SHA-256:

- `normal.abc`: `6e1c152acb0db3b551c28655f65fd930b09e91c1a4df094dc66494bc749e5753`
- `phased.abc`: `ecccd37b99b155bc71b1f43c4c5c12cdbc3fe07dead4f15451185a72254c83f5`
- `phased_array.abc`: `d0081a07b358ed96af9212249514540f4e6995907a0a5104c3eeca316fbf7401`

`phased_array.abc` uses the same command with `--record-name=phased --source-file=phased.js`; its source was the earlier array-return version of `phased.js`. It is deliberately checked as a separate current literal/evaluation refusal (the baseline cannot lift delete at all).
