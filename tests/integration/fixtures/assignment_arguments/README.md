# Assignment arguments

Independent JS source oracle and es2abc-produced dynamic ABC, using DevEco
Studio es2abc (`--bc-version` 24.0.0.0). Compile from the repository root:

```sh
/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc --module --merge-abc --opt-level=0 --record-name=NAME --source-file=NAME.js --output tests/integration/fixtures/assignment_arguments/NAME.abc tests/integration/fixtures/assignment_arguments/NAME.js
```

`normal` and `phased` have STOBJBYNAME at PC52, `multiple` at PC52/86,
`computed` has STOBJBYVALUE at PC78. The CLI test independently decodes the
compiled run method on every run and asserts these [[Set]] instruction kinds
and counts. These are ordinary assignments, not class fields/[[Define]].

The harness compares original source with generated source under both numeric+
constructor and language-source flags. It logs callee getter, target getter,
RHS, key evaluation/coercion, setters, receiver and call. It covers setter/RHS/
read-only-property/callee/key throws, multiple assignments, and callee rewrite.
Setter return values deliberately differ from RHS; assignment must return RHS.
`phased` catches the sentinel and observes the exception, while `computed`
evaluates key and RHS ahead of the call so observable key coercion retains its
position relative to RHS. General effectful RHS after an unknown computed key
still needs the existing reference snapshot proof and may be rejected.

Mutations replace the stored RHS value and detach the receiver: both must alter
the oracle trace. Node is a source-language oracle, not execution of Ark ABC.

`computed_inline` adds a conservative refusal check for a computed key call
inside the assignment with a literal RHS. It still reports typed kNotInline;
this increment does not claim every computed assignment can be fused.

Compiler SHA256: `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`

| File | SHA256 |
|---|---|
| computed.abc | `232c1cd7a4b8e7c4dddf6c14ad80dfaad37af8ce0aceb554af3bfd97a8b2ffff` |
| computed.js | `3a8900b78578a6c39649c5708080f540a2d7a001f44599f15b04ea882d4b8759` |
| harness.mjs | `e74512c8737f28ade6b75ca15a86f3f0d271f97ff3557a0c3bd3dd3da090d103` |
| multiple.abc | `287a79534dbd9605bf1df4338e383372e047fe7bd68818238b7b7488f0b57dd7` |
| multiple.js | `98a821f56ee1650663728809fe587745ce06fb58620bfa66adb01a3f5c7916fd` |
| normal.abc | `c6a2dba46edf6e9006aede84d276abcd268067501b30a74ddad9c447f4717b2b` |
| normal.js | `2c306bb3e470fa77f98f9be42229b933f1d03274945aac8be6066cb21f7302b9` |
| phased.abc | `786eabb099db548a74a8346dc1c65ca6274dbfeccffab88f8c36b651771d6179` |
| phased.js | `078f6177fde8a138b8d99b608ae3b8db2a83826a28716092fa84f5f65d1c7c48` |
| computed_inline.js | `54d7eac2d29f662413389f637d24343aa6bfff61876f0b3163606f3c4322eb68` |
| computed_inline.abc | `552ed9ad1e39c2fd9f6237e1266edb74d1c91266bf6523ef60db576f42ae755f` |
