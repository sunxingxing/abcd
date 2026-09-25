# Method Position Transport

The unified body printer already records original-PC spans, but FunctionWriter
previously discarded them when embedding body text into a callable wrapper.
The first transport now forwards these typed spans through CaptureBuffer to
the enclosing SourcePrinter before the corresponding body bytes are written.

MethodGeneratedSpan pairs the original ABC method entity with GeneratedSpan.
PrintedSource owns a separate descendant vector: intermixing descendants with
entry spans would invalidate existing fused-origin distance/index proofs.
ClassBuffer and LeafBuffer relocate incoming endpoints using the printer's
current byte, line and column. The first child line inherits the parent's
column; subsequent child lines retain their own column. Byte and line offsets
are added with overflow checks. This uses emitted-position state, not searching
or parsing generated JS. Repeated emission of one method can retain multiple
spans distinguished by their generated ranges.

The admission boundary is deliberately explicit:

- Bound generic bodies forward their own spans and any retained descendants.
- A generic template forwards its own spans when the receiving sink can
  resolve its captures into a bound source file.
- A receiving template printer does not collect descendant coordinates yet.
- Legacy writers, buffered leaf-fragment caches and other streams without a
  position owner do not claim descendant coverage.

For captures resolved by a bound-source sink, FunctionWriter samples the
actual printer cursor immediately before and after each capture emission.
Only same-line insertions without unresolved capture-cursor changes are
accepted. Byte and column deltas must agree. Their checked cumulative lengths
form a prefix table. RelocateCapturePosition, shared with BindSourceTemplate,
uses the endpoint's capture cursor to select insertions before that endpoint,
including multiple zero-width tokens at one template byte. Only insertions on
the endpoint's original line affect its column; the total prefix affects bytes.
Finally the saved body-start position relocates the endpoint into the record.
No binding name is predicted and no generated JS is parsed for locations.

Prefix capacity is admitted alongside retained template text, PC spans,
capture tokens and any owned host-realm binding. The temporary table remains
bounded. This adds O(C + S log C) work and O(C) scratch for C captures and S
spans. Nested template-to-template propagation and fragment replay still need
an ordered position-event transport. Omitted methods remain omitted, not
mapped to a parent's span, and `complete` stays false.

Method span buffers grow geometrically, with the old and full replacement
buffers simultaneously admitted alongside current text, entry spans and
capture capacity. Retained growth reduces the remaining printer allowance.
Relocation is O(S) in received spans, with amortized O(S) vector growth; no
per-span scan of emitted text or instruction list is performed.

The CLI preserves `.entry-positions.json` unchanged and additionally streams
`.method-positions.json`, referenced by the record manifest. Its scope is
`partial_methods` and `complete` is always false at this stage. It contains
method entity, original PC, origin kind and final-file endpoint coordinates.
Neither this artifact nor its presence upgrades source completion/executable
status or establishes runtime equivalence.

## ABC-Only Evidence

Capture-resolved transport builds in both production CLIs. Read-only review
identified the owned host-realm binding as additional live prefix scratch;
its capacity and terminator were charged before final export. Export
`build/pasteboard-capture-positions-01` carries 3,445 spans for 94 methods,
adding Home methods 5684 and 6322 and ProgressBar method 10639 (initialRender).
Compared with the previous export, only Home/ProgressBar method-position JSON
changes; all generated JS and entry maps are unchanged. Source/refusal status
remains unchanged and full mapping/runtime equivalence remain unproven.

Initial uncaptured-transport evidence:

Both production CLIs built successfully. Review found a missing entry-span
capacity charge during descendant-map growth; this was fixed and reviewed
again before export. `build/pasteboard-method-positions-01` contains 3,315
descendant PC spans covering 91 distinct ABC methods. Logger contributes 150
spans for constructor 4740 and methods 4604, 4631, 4658 and 4685. Constructor
PC4 maps to bytes [394,405), lines 30-31 in the generated Logger file, the
emitted `$r2 = $r8;` statement.

Directory comparison against `build/pasteboard-export-names-01` shows only
the eight new method-position JSON files: generated JS and entry maps are
unchanged. Eight incomplete application records and both host-constructor
refusals at PC14 remain. These observations do not establish complete mapping
or runtime equivalence.

Tests, generated JS execution and RSS remain paused. Deferred regressions
include multiline/prefix relocation, repeated method expansion, zero-width
anchors, await-protocol spans, multiple captures at one byte, resource/overflow
failures and fragment replay. Full descendant coverage remains unfinished.
