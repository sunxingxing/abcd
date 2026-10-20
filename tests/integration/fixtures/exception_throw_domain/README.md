# Exception throw-domain fixtures

`build.json` pins the DevEco es2abc binary, commands and source/ABC SHA-256s.
O0 and default optimization are stored independently. Sources are evaluation
oracles and compiler inputs only, never decompiler inputs.

- `close`: the original Hole/close-error phi, a non-Hole conditional throw and
  a user catch observing error properties. Inputs now pass; close topology is
  still refused with located kMultipleExits.
- `member`: the same method inside a class. It stays a partial class with the
  exact new omission cause; its unrelated good() remains recoverable.
- `loop`: an isolated break-close, retaining the independent ownership refusal.
- `handler`: nested user catches/rethrow, with a source catch binding. O0
  engine output passes Node differential; default optimized input retains the
  pre-existing CatchRegionError. Recompiled O0 engine output is strictly
  bytecode-identical to the independent source at both SDK optimization levels.

The unit test separately asserts the raw payload refinement on the close ABC.
The integration test never treats a refused/partial query as a Node pass.
