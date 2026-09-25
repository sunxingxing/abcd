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
- A generic template forwards its own and descendant spans through both bound
  source and template sinks. Template endpoints retain unresolved cursors.
- Legacy writers, buffered leaf-fragment caches and other streams without a
  position owner do not claim descendant coverage.

For captures resolved or forwarded by a position-aware sink, FunctionWriter samples the
actual printer cursor immediately before and after each capture emission.
Only same-line insertions with monotone unresolved capture cursors are
accepted. Byte and column deltas must agree. CaptureShift holds separate
cumulative byte and unresolved-token counts. RelocateCapturePosition, shared with BindSourceTemplate,
uses the endpoint's capture cursor to select insertions before that endpoint,
including multiple zero-width tokens at one template byte. Only insertions on
the endpoint's original line affect its column; the total prefix affects bytes.
The selected prefix's unresolved count becomes the endpoint's new capture
cursor. Finally the saved body-start position, including its capture cursor,
relocates the endpoint into the parent. Each template owns its descendant
spans until the next insertion/replay; only the bound-source boundary requires
all capture cursors to be zero. BindSourceTemplate also relocates descendants.
No binding name is predicted and no generated JS is parsed for locations.

Prefix capacity is admitted alongside retained template text, PC spans,
capture tokens, descendant maps and any owned host-realm binding. The temporary table remains
bounded. This adds O(C + S log C) work and O(C) scratch for C captures and S
spans. Work across owners is proportional to propagated spans at each layer;
the existing CreationExpansion limit bounds ancestry to 32 layers. Fragment
cache replay still lacks this transport. Omitted methods remain omitted, not
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

## Recovered Static Fields

StaticField optionally retains a StaticFieldOrigin containing the initializer
method entity and the exact receiver-field definition PC. FieldReader records
it when AddField accepts that definition, not from a name match or generated
text. Existing sizeof-based field capacity accounting includes the new value.
Manually supplied fields without an origin do not fabricate one.

ClassWriter samples the position-owning sink immediately before and after the
static field statement. MappedSpan forwards these already owner-relative
coordinates without adding the current cursor a second time. Template capture
cursors survive the same descendant transport as ordinary method spans. Dry
streams without positions collect nothing. This only maps the field definition;
literal construction, ABI transport and return instructions require separate
origin evidence and remain unmapped for this recovered initializer path.

## ABC-Only Evidence

Static-field origin transport builds in both production CLIs and read-only
review found no concrete defect. `build/pasteboard-static-positions-01` adds
initializer 4439 PCs 13, 21 and 29 at CommonConstants lines 31, 32 and 33.
The original instruction listing confirms these three definition sites.
Coverage is now 195 methods / 9,745 spans. Only CommonConstants' method map
differs from the preceding export; JS and entry maps are unchanged. All method
entities in the eight published records now appear in either entry or method
artifacts, but complete per-instruction coverage and endpoint correctness have
not been established. The source completion/refusal statuses are unchanged.

Earlier nested-template evidence:

Nested-template transport builds in both production CLIs and read-only review
found no concrete defect. `build/pasteboard-nested-positions-01` has 9,742 spans
for 194 methods, up by 100 methods. Only the six page method-position files
change versus the previous export; all JS and entry maps remain identical.
Per-record method counts match all non-entry methods for the six pages and
Logger. CommonConstants still omits static_initializer method 4439 (its entry
4329 is mapped separately). This count check does not prove every original
instruction is mapped or that every endpoint is correct. Status remains eight
incomplete application records and two constructor refusals at PC14.

Earlier capture-resolved transport evidence:

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
