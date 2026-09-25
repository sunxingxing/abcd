# Class Preparation and Publication

## Design

The approved common source pipeline must preserve captures and original PCs
when class syntax changes. `ClassSource` is a private, bounded output template
between the existing typed class/method writers and their creation-site owner.
It stores emitted text, typed capture occurrences with domain requirements, and
method spans in `PrintedTemplate` coordinates. It is not a parser or semantic IR
replacement. Class bodies still consume the existing analyzed source AST.

`ClassWriter` prepares a class body once and gathers `SelfBindingUse` for its
ordinary buffered and attached methods. If all are Unused, the result is the
direct named class expression. Otherwise it retains the immutable MethodSelf
captures and requires the existing arrow wrapper. Unknown is not Unused.
The selected prefix is published only after preparation succeeds. This also
removes the class-level dry emission previously used for ordinary streams.
Existing method-level validation/fallback protocols remain unchanged.

Alternatives rejected: searching/replacing emitted JS would lose semantic
ownership; a second method analysis merely to choose the prefix duplicates
work; deleting all wrappers would break required function-object captures.
The template uses existing standard C++ containers and project capture-position
machinery, with no new dependency or license obligation.

## Captures and Positions

Capture tokens retain slot and domain, including zero-width occurrence cursors.
Preparation never resolves an outer capture or relaxes its domain. Replay calls
the original creation owner, which resolves local/outer cells with the same
proof as before. A plain output stream is not granted capture support merely
because the private template supports it.

Normal method spans are based at their next text chunk; already measured static
field spans retain template coordinates. Nested captured templates first apply
their measured insertion prefix and then their class-relative origin. Replay
adds the selected wrapper prefix and final capture insertions. Insertions must
be newline-free and preserve the byte/column delta invariant. No input sidecar
map or output-text search contributes to these positions.

Preparation errors leave the caller untouched. Replay IO/allocation failures
and creation-site capture failures still require atomic artifact publication.
Output-limit admission includes the selected wrapper before publishing bytes.

## Bounds

The remaining class byte allowance is split equally between retained template
storage and current method preparation. This is a conservative admission policy,
not permission to exceed the process cap. The template charges text capacity
and terminator, capture/map capacities, and simultaneous replay prefix storage.
Geometric growth admits old plus replacement allocations. Insufficient space
returns typed resource exhaustion; no semantic fallback discards positions or
captures. Large methods may now hit the smaller preparation allowance earlier.

For B emitted bytes, C capture occurrences, S positions and M members, the
additional work is O(B + M + C + S log(C + 2)); retained space is O(B + C + S).
Nested classes are charged at every enclosing template where their text/maps
are materialized, not claimed to cost only the final file size. Existing nested
expansion budgets remain active. Each class adds one preparation/replay layer,
not an extra body decompilation pass.

## Evidence and Limits

Production `abc_semantic` and `abc_structure` build succeeded. ABC-only export
`build/pasteboard-class-template-01` removed wrappers from Logger and
BreakpointType: 8,035 lines / 137,808 bytes versus 8,041 / 137,914 previously.
The six page source files and their position artifacts are byte-identical to
`build/pasteboard-argument-values-01`, including their captured/nested methods.
Statuses remain eight incomplete source records, two constructors refused at
PC 14, and nine metadata records. Partial method maps retain 195 methods and
9,745 spans. Logger method 4658 PCs 12/14 point to bytes [363,371), `$import0`;
PCs 22/70 point to [363,424), its full debug call.
After final output-limit admission changes, production rebuild and export
`build/pasteboard-class-template-02` succeeded; its entire sources directory is
byte-identical to the first export.

Independent read-only review found no concrete correctness issue. Tests,
generated JS execution and RSS measurement remain paused by user request.
Deferred regression coverage includes mixed self requirements, nested capture
domains, same-byte multiple captures, static-field spans, plain/unpublished
sinks, stream failures and tight budgets. These artifacts do not prove full
source equivalence or process peak-memory compliance.
