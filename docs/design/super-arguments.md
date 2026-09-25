# Native Super Arguments

The shared AST records an optional SuperArgumentRegion for the one admitted
range-super call. It retains original instructions and statements; the region
only changes ownership of pure argument reads during emission. This is not a
pasteboard-name rule or a second constructor writer.

Admission requires a same-block suffix consisting of one original accumulator
load and adjacent register save per explicit argument, in range order. The
folded assignment must still read that same private source register and target
the expected argument slot. Each retargeted save must have exactly one original
SSA use, the corresponding super argument; the accumulator load must have only
its save use. Handler and phi observers therefore prevent removal. Merged
origins, field targets, non-stored results and non-copy instructions refuse.
Sources may not overlap the argument destination window, so no retained read
depends on a removed earlier write. Phased/catch methods and multiple super
sites refuse; zero-argument calls retain the existing printer.

The suffix has no user effects, allocation or coercion. Delaying its private
reads until the native argument list cannot change their values: every suffix
write targets only that disjoint argument window or the private accumulator.
The super result commit and existing constructor/initialization checks stay at
their original locations. This does not change parent-brand requirements or
admit spread super, arbitrary default expressions or pre-super native this.

The printer emits empty anchors for the owned load/save suffix and reads its
sources through the existing Value binding/slot-observation path. On emitting
super it relocates the two anchors for each argument to the actual argument
text, checking the original PCs at the expected suffix positions. The call and
its result-save origin remain independently mapped. No generated text is parsed
and no intermediate source buffer or argument vector is allocated.

The proof scans instructions once and the sole candidate suffix once, O(I+A)
time and constant additional storage, for instructions I and arguments A.
Emission performs O(A) existing argument work and constant-time span indexing.
The optional fixed-size region is part of SourceAst; existing statement/map
and slot tables retain their bounded admission.

Tests, generated JS execution and RSS remain paused. Deferred regressions
include mutated/overlapping sources, extra uses and phi uses, zero/many/narrow/
wide arguments, live accumulator values, malformed origins, branch entries,
multiple super sites, throwing super, argument/result slot overlap, budget
refusal and exact per-argument PC positions. Production export is not proof of
complete application equivalence.

## Production Evidence

Both production CLI targets built. ABC-only export
`build/pasteboard-super-arguments-20260925` uses direct argument reads in all
six page constructors. Five print `super(parent, __localStorage, $r6,
extraInfo)`; Home retains its existing local sources in the native argument
list. Totals are 3,867 lines / 86,388 bytes, down 32 lines / 392 bytes from
the super-slot-audit export.

PageA method 7201 PCs 63/65 map to [2527,2533), `parent`; 67/69 map to
[2535,2549), `__localStorage`; 71/73 map to [2551,2554), `$r6`; 75/77 map to
[2556,2565), `extraInfo`. Super PC 79 and result-save PC 83 retain the full
assignment span [2514,2568). Partial maps retain 195 methods / 9,745 spans.
Statuses remain eight incomplete records, two constructors refused at PC 14
and nine metadata-only records. Read-only review found no concrete defect.
No tests, generated JS execution or RSS measurements were run; this is output
and provenance inspection, not a complete semantic-equivalence certificate.
