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

## Requirements-only preparation fixtures

`forward` is an anonymous class in a normal `factory` function. `factory` has
no direct lexical access; its member `read` demands `outer`'s parameter.
`forwardwidthN` creates N such classes. `forwarddepthN` puts N ordinary nested
function occurrences inside the class member before the actual captured read.
The generator above uses the same pinned compiler/options for all of them.

These fixtures test demand collection, pure forwarding, actual creator-storage
binding and the requirements-only publication gate. They do **not** demonstrate
production descendant-class output or a Node differential. The named class self-binding variant was refused by normal input with
SemanticError::kExceptionRegions; it remains outside this normal-creator test.
No handler was removed to make it pass.
Exact budget checks cover collection, family cell planning and binding. The
class-member family read is exercised at each depth, but its full preparation
budget is not the constant-size binding budget reported for the deepest case.

| File | SHA-256 |
|---|---|
| forward.abc | `2b7552a803d8651752677371f22f52d20ee0231ff381372ed74681b390cc0c49` |
| forwarddepth200.abc | `e74919d50739994f615c5d1b59565ecf49f64b045e65c006fe2a823eba776174` |
| forwarddepth201.abc | `446bdd2940167c2d4cbc7dac67cfab45f0109eb6ca7767949f24d886458b7dff` |
| forwarddepth400.abc | `c1427d001901d9d25e017a687e89a0532fc0f2757a79ed7c0314c0a43acbf5ad` |
| forwarddepth800.abc | `4ff4c04cdf9167a1191b924489feb6f868a8edaccf2a9dff58ad98d28586088e` |
| forwardwidth200.abc | `78e155a42661c89e9348216096c1bf4eb7a04a6a142948367968c14bbf15a786` |
| forwardwidth201.abc | `981b61a6cac0e1fe7c9c0a15647d3ed9e3140e685c91bd7c20b3053c10324764` |
| forwardwidth400.abc | `83e55fefb613e26162d06fd9b8e2062f13a59be0ca52e7711c0a8aa2a332bee5` |
| forwardwidth800.abc | `70e267f65710f0b91ee16b05ad1e7798667adbf7915c8d88ccb851db20046ee4` |

## Closed descendant production (640dd19f follow-up)

`closed` is exported by the engine under both contracts and compared in Node
with its adjacent source. It covers a local class in a function and in a class
method, construction and instance [[Set]], an inherited Object.prototype setter,
a throwing constructor, repeated loop creation with distinct identities, and
copy/branch variants. A class method calls global `$value0`, deliberately using
the generated-local spelling to check creator-enclosure name reservation.
The test checks real class-creation instructions first;
mutating [[Set]] into [[Define]] or removing the throw must change the trace.
The engine never reads the JS oracle. The new files use the same pinned compiler
and command as above; generate.py reproduces them.

`member` completes since 2026-10-23: its capture requirement binds to the
creator's existing parameter storage unchanged. `deferred` requires a late
declaration and stays refused separately. `heritage` stays refused by the new
creator scheduler. `nested` is a class member which itself creates another
class: requirements-only discovery retains a located nested-preparation gap,
rather than recursively finishing an unbounded family graph.

The width/depth unit tests now enter production family discovery and class
preparation and measure exact byte/work acceptance boundaries at every size.
These budgets include each tested phase's storage; they are not a claim that
all whole-record verification fits at arbitrary width. In a separate export
probe width 200/201/400 and depth 200/201/400/800 complete; width800 reaches
FamilyEnclosureError::kResourceLimit under the unchanged record cap. Depth800
is valid syntax with Node --stack_size=8192 --check; Node's default parser
stack is exhausted on the generated 800-function expression nesting; the
original declaration fixture passes the default parser stack.
This is not a decompiler C++ recursion or a raised engine limit.

| New ABC | SHA-256 |
|---|---|
| closed.abc | `0362108d33345a6f1efc46694ab601a097d2251bf0ddb59194da0142fb16a3b6` |
| deferred.abc | `7a360f1a53411dd2e28b6c44b9df108286e6e9cd0343c0b8e426ae5e6504f39b` |
| heritage.abc | `35eaafd8e78329213fe3f4520dd558c92e6de846fbcd70e8e8c9bc0738ae0953` |
| nested.abc | `a411567312fa4529d17cec350cac442ec32d3fcfa18be98565bcc96cc983d358` |
