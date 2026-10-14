# Creator-class preparation fixtures (increment I)

Generated locally from the adjacent JS by `generate.py`, using DevEco es2abc,
`--module --merge-abc --opt-level=0 --record-name=NAME --source-file=NAME.js`.
The generator records the full compiler path and passes `--output NAME.abc`.
Run `python3 tests/integration/fixtures/creator_classes/generate.py` from the
repository root. No paired source is read by the engine or the C++ test.

- `widthN`: one normal descendant creates N distinct local classes. Discovery
  resolves every actual class pool/constructor occurrence before the gate.
- `depthN`: N nested ordinary function occurrences, then one local class.
  The real family DFS must reach creator occurrence N without C++ recursion.
  This tests discovery depth, not completed nested-class body preparation.
- `member`: a record-root class method returns a local class capturing its parameter; the nested creator reaches the gate as member-family occurrence zero.
- N is 200, 201, 400, 800. Compiler-generated debug/encoded names grow with
  lexical depth; complexity is measured against total input size, not just N.

`creator_classes_test` verifies actual discovered class counts, constructors,
creator identities, gate locations, pool mutations, duplicate occurrences,
owner-range collisions and exact discovery/verifier work and byte boundaries.
The separate production family traversal reaches the located preparation gate.
There is no source publication and no Node equivalence claim in increment I.
Owner ranges remain unassigned until all member family counts are known; the
record-root preparation and its range verifier exercise actual reservation.

Compiler SHA-256: `4d921b123d050309231436be967860a2facb79cd2c241b5b37f3b08f74b54bca`.

| File | SHA-256 |
|---|---|
| depth200.abc | `a7da053f9a75a108bb3a6f220fd8fd6bb867505d1b266a273763bfac737bada7` |
| depth201.abc | `f53df8aad1aab7ed9fb911384a674821b1873fa87e5dfa63fbf4bdfdf51d81a7` |
| depth400.abc | `271855fed3d495e35ade65c04ae7186c7ad1066e2fd8ab80eeb73de29babae11` |
| depth800.abc | `e0987a5584bc66b75fa31effca7dfd5e1154ec5974fa258d2778f2ddf7af6f3d` |
| width200.abc | `717a46f868eaea07cf9a896067cc9b333b45c3622cf589d6d8e788e68e0c5667` |
| width201.abc | `55fee412cb681ef41c780f94b7bdb24457f0fdfc249c27741d66193d1926589b` |
| width400.abc | `5a3641e7adda3dc3f663f9b41d654cdec95d0362eab0698c4381c064c4ff3d44` |
| width800.abc | `5b2684191046c044eafa48732ee3d1692e039b21be4be56e6947db85f76247db` |
| member.abc | `afbae48163c7236e546a055e7522f40a4477fedede7a44d2ac00a6f564a24548` |
