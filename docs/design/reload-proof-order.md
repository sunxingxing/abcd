# Reload Proofs After Result Retargeting

## Failure

PasteBoard method 9193 exposed a composition error in the common AST pipeline.
At PC 51, createData writes the accumulator; PC 56 saves it to r5 and PC 58
reloads r5. ElideKnownReloads removed the reload because the emitted save proved
that acc still held the same value. FoldStoredCalls subsequently retargeted the
call result to r5 and removed its accumulator commit. The later getRecord call
then read a stale accumulator. Original SSA still reported the save as the
call's sole direct use, so that count alone did not detect the stale proof.

The same method's fromStyledString invocation at PC 257, save at PC 261 and
reload at PC 263 exposed the equivalent conditional-read failure. This is a
real correctness regression introduced by late call-result retargeting, not
merely output style. The stored-call and nested-call export evidence documents
only successful export/positions; it must not be treated as semantic validation.
Buffered-object result retargeting can invalidate the same reload fact.

## Ordering Contract

All result retargeting and result-commit elimination now precede
ElideKnownReloads, including array/class/return recovery, stored calls, unused
results and object initialization/copies. Known reload analysis sees the final
commit shape and resets its local physical-accumulator fact at retargeted nodes
and anchors. It also explicitly rejects non-stored SourceAssignment results.
Only afterward do binding receiver and argument consumers run, using their
existing exact-use/transport proofs. Do not move a result-retargeting pass below
known reload analysis without updating or invalidating the physical slot proof.

This is a pass-composition correction, not a weaker semantic premise or a
source-text repair. SSA identities continue to describe the original operations;
physical source slots must additionally reflect which commits survive in AST.
No new allocation, dependency or graph traversal is introduced. Existing pass
complexities and memory admission remain unchanged.

Tests, JS execution and RSS remain paused. Deferred regressions must cover call
and buffered-object results with a retained register plus an eliminated reload,
branch/member reads of acc, additional phi consumers, nested argument results,
non-stored assignment facts, and preservation of original reload PC anchors.

## Production Evidence

Both production tools rebuilt successfully. ABC-only export to
build/pasteboard-reload-proof-order-20260925 succeeded. Compared with
pasteboard-nested-call-arguments-20260925, only PasteBoard source and its entry
and partial-method position files differ: getRecord now uses r5 as its receiver, and the
fromStyledString condition regains `$acc = $r4;` before inspecting acc.
PC 58 maps to the emitted r5 receiver [18291,18294); PC 263 maps to
[18605,18617), the restored accumulator reload including newline. The condition
PCs 267 and 270 map to the following if header, not the previous call.

Totals are 4,205 lines / 90,322 bytes. The map still contains 195 methods /
9,745 spans. Eight records remain incomplete, two host constructors remain
refused at PC 14, and nine records remain metadata-only. Read-only review
confirmed the stale-proof mechanism and found no remaining concrete example
in the reviewed change. These inspections do not replace the paused semantic
tests or prove overall equivalence; no JS or RSS checks ran.
