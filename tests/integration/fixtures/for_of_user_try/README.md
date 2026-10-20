# A native for-of enclosed by an ordinary user catch

`build.json` pins the SDK binary SHA-256, exact default/O0 invocations and all
source/ABC hashes. Sources are independent evaluation oracles; only ABC paths
enter the engine.

- `main`: normal/break/Return/explicit body Throw, with an effectful user catch.
  The Node test traces cached next, getter/call failure, missing/null return,
  primitive return result, original Throw priority and exact caught identity.
- `member`: the iterable is obtained by a getter. The frontend emits a
  transparent acquisition-input rethrow nested inside the user try. Its route
  must end at the retained user catch, not at caller exit. Node also throws
  from that getter before iterator acquisition.
- `async`: await in the body, surrounded by the user try. The separate raw
  resume/rejection correspondence remains refused, not counted as a pass.
- `finally`: a source finalizer surrounding the loop/catch. The separate
  pending-completion/finalizer correspondence remains refused.

The synchronous engine outputs, not copies of the fixture sources, are loaded
by Node. Original and engine-emitted modules are separately recompiled with the
pinned SDK by `tests/tools/for_of_multiple_roundtrip.py --fixture main|member`,
using grouping_bytecode.py's strict assembly reader. Every differing method
and its unnormalized assembly diff is retained in the report.
