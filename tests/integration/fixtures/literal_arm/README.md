# Conditional literal member arguments

`calls.abc` is compiled from `calls.js` with the pinned DevEco dynamic es2abc:

```
es2abc --module --merge-abc --opt-level=0 --output calls.abc calls.js
```

The absolute compiler identity, SHA-256, options and source/ABC hashes are in
`manifest.json`. The engine receives only the ABC. `literal_arm.mjs` compares
its complete record output against the original source in separate Node
processes: 140 scenarios cover both arms, ordinary truthy/falsy values,
property getters, throwing lookup/arguments, callee replacement, receiver
identity, return values and property descriptors. No exotic runtime-equivalence
claim or new source-language premise is involved.
