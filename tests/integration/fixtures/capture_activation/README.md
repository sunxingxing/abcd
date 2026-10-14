# Cross-block guarded-capture activation

Original fixtures authored for Round 68, with no corpus source as engine input.
`branches` allocates its environment before a branch, creates a class reading
an uninitialized outer binding, creates another class, then initializes that
binding. `variant` changes the branch/copy form. `loop` allocates a fresh
lexical environment on each while iteration and retains readers from all
iterations. The tests execute early reads and throwing initializers; an early
read must remain a ReferenceError, and retained readers must retain distinct
iteration values.

Rebuild with the pinned local DevEco tool (from this directory):

```sh
ES2ABC=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/ets/build-tools/ets-loader/bin/ark/build-mac/bin/es2abc
for name in branches variant loop; do
  "$ES2ABC" --module --merge-abc --opt-level=0 --record-name="$name" \
    --source-file="$name.js" --output "$name.abc" "$name.js"
done
```

The CLI test first confirms real class creation, named HoleGuard and lexical
environment allocation in the ABC. It exports with both contracts, runs Node
on the engine output and this source with identical hooks, and compares all
traces and exception constructors. Exception messages/stacks are outside the
oracle (the Ark hole-check and Node native TDZ messages need not be identical).
No finally lowering or alternate Ark protocol is used by these fixtures.
The declaration-hoisting mutation remains syntactically valid and must change
the observable TDZ/iteration behavior. A second loop mutation preserves the
early ReferenceError and all earlier traces, but returns one shared later value;
retained readers must detect that activation error independently of TDZ.

The production rule is original-flow dominance plus unique store, Language
writes, and independently verified source activation/scope; these opcode
assertions are fixture checks, never production admission.

SHA-256:

```
4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca  DevEco es2abc
2a8924a671fbefc10df30bb4526092de7cc95d61af5a2ec8d7fca0413ce40b0a  branches.abc
4ee2572834683bfbc439603e62db1c6428f648587fec1c8389eff2a68f85e8fa  branches.js
be4a4fa6c4f2b2256e4fbee60912984116b19303358881684146c23031958d21  loop.abc
5568554bd4ece700dd292fed263f08cc123d846054652dde8ac99d7b69b12d9f  loop.js
164ced202254f07825b15b7347d758b13bc9d89b549b8dac38b99b1a7ef658e5  variant.abc
986e18dbc0dd0300a34c57ed05860afcaf14c52408f005ef20fce7907b881c21  variant.js
```
