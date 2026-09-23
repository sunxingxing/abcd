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

Immersive record 16474, caller 16638, defines `HomeModel` and an arrow target
at method 16610. Its entry ABI-context copies are dead. Data-object-buffer
creation and guarded fresh-object fields recover the three property writes at
pc38, pc68 and pc98. The module load between writes has a typed normal
register effect: it replaces acc and leaves private caller registers intact.
The deferred named `CALLTHIS1` at pc109 is now recovered as a native receiver
call with its intervening argument construction. pc113 is `LDUNDEFINED`, not
`NOP`; the narrow tail proof permits both operandless instructions. The class
site now reports `sites=1 ready=1`, and `home_model.mjs` checks constructor
state, imported constants, callback fields, and repeated initialization.
After this extension, `ctest --test-dir build/decode --output-on-failure -j4`
passed 116/116. The class-site fixture measured 9,088 KiB peak process RSS
(`time -f '%M'`), not a full-corpus or whole-decompiler memory bound.
This is a class-site fixture, not an executable record exporter. Instance-field
lifting outside this constructor pattern, global variables, complete source
signatures and full function-body integration remain open.
