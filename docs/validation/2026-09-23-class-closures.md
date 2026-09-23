# Class-body ordinary closures (2026-09-23)

`WriteCreatedClass` now supplies its existing method lookup to constructor and
ordinary member body emission. The bounded leaf-function path still resolves
each `DEFINEFUNC` target, checks record/module ownership, binds captured cells,
rejects unsupported function kinds, and preflights the complete class before
publishing output. The lookup is borrowed only during synchronous emission.

The synthetic class fixture exercises ordinary nested functions in prototype
and static methods and in a base constructor. The Node oracle verifies fresh
function identity, name, formal length, and parameter return behavior for the
two member placements. Disconnecting the lookup made the new unit assertion
fail. Later guarded arrow-source support admits a subset of nested `kArrow`
targets; observable ABI-context reads and conflicting method roles still reject
without partial class output.

`ctest --test-dir build/decode --output-on-failure -j4`: 113/113 passed with
`ABCD_PAIRED_CORPUS_ROOT=/home/rabbit/sample/abc_with_code`. On the real
MultiCommunityApplication class-site fixture, 6/32 sites were ready and peak
process RSS was 9,984 KiB (`/usr/bin/time -f '%M'`). This is not a full-corpus
or whole-decompiler memory bound measurement.

Immersive record 16474, caller 16638, still rejects its HomeModel class at
member `initTabBarList` offset 36. That `DEFINEFUNC` resolves to method 16610
with initial `kArrow` kind. Its entry `MOV` copies of ABI context are dead and
now pass the two-pass proof, but this nested creation still fails at the arrow
body's pc30 `CREATEOBJECTWITHBUFFER` (typed
`LinearLexicalError::kInstruction`). Later data-object-buffer source support
passes that instruction, but the next rejected operation is pc38
`DEFINEPROPERTYBYNAME` (`LinearLexicalError::kInstruction`). That operation
remains unimplemented, so this is not a ready class site.
No executable record exporter, instance-field lifting, or full-source
signature recovery is claimed.
