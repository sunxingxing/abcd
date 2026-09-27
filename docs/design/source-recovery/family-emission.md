# Owned Function-Family Emission

`PreparedFamily` now retains one `TreeVerified/Incomplete` route receipt per
occurrence, only after its actual input, IR, tree, capture/write, ABI, source-site
coverage and destination checks succeed. Same-method occurrences remain distinct
members; receipt arrays are indexed by family member, not by method entity.

`FamilyEmission::Write` consumes this immutable owner. It has no constructor
accepting raw trees, diagnostic counters, serialized receipts or supplied spans.

## Publication Sequence

1. Admit the emission object and receipt array in the caller's scratch allowance.
2. Validate every prepared receipt and match its identity, method and revision
   against its immutable member tree. Require ValueTree/TreeVerified/Incomplete.
3. Bound output by both its byte budget and remaining destination-offset range.
4. Recheck the actual destination, including typed grammar (`kClassMember` or
   `kFunctionExpression`), strictness and complete global binding context.
5. Run the common iterative printer. No source reader, legacy writer or source
   reparsing is involved.
6. Check every member's printed identity, revisions, node-array dimensions,
   owner range and original instruction disposition. Emitted nodes require
   nonempty ranges inside the owner; real anchors may be zero width; proved
   pure-elided instructions have no generated range.
7. Only after the entire family succeeds, produce per-member Printed/Complete
   receipts and own them together with the original prepared family and printer
   positions. Original prepared receipts remain TreeVerified/Incomplete.

Evidence rules are bumped from 23 to 24 for this new publication boundary.
Ordinary emitted expressions use `kFunctionExpression`; the unchanged diagnostic
entry still uses `kConditionalExpression` and cannot grant these receipts.

Complete here is METHOD coverage, not a whole class/record, independent test
acceptance, or arbitrary execution-environment permission. Root creation context
and destination obligations remain attached to the owning artifact. Record
assembly must preserve the actual embedding; copying receipt JSON alone is not
authorization to move the code under different bindings. No module initializer,
unsupported child, constructor, exception or async body is silently substituted.

## Positions and Failure

Generated positions stay relative to the full family output; `Destination()` is
the one enclosing offset. Child functions retain their nested absolute positions
within that output, without rewriting every span or duplicating ancestor PCs.
Caller stream storage and the already-owned prepared input are caller-admitted.
Added receipt arrays, printer scratch and checks share one decreasing allowance.
`AllocatedBytes` conservatively includes earlier preparation scratch charges;
it is not process RSS. Per-instruction checks are linear and allocate no PC map.

Any write, resource, position, context or receipt failure returns no emission
owner and no completed receipts. A failure after writing can leave partial bytes,
which the caller must discard. Offset overflow reduces output admission before
addition and cannot produce wrapped source ranges.

## Validation Status

Added specifications cover ordinary/nested families after input-file release,
class/default/object methods, prepared-versus-printed receipt states, root
grammar mismatch, output exhaustion, zero memory and destination overflow.
Receipt parsing rejects prior rules 23 and uses the current rule constant for
identity-free/overflow tests. Tests, generated-JS execution and RSS remain paused.
CLI family drafts are deliberately not promoted by this implementation;
whole-record integration and end-to-end execution acceptance remain outstanding.
Production builds exited 0. ABC-only inspection retains the existing 7559 method
segment [2638,2924), now serialized under rules 24, and the unchanged 1434-byte
four-member 7449 incomplete/nonexecutable draft. Those observations do not
exercise the new emission entry and are not its execution acceptance.

## Record Segment Transport

`FunctionSegment` carries a typed variant of shared immutable `FunctionEmission`
or `FamilyEmission`. `ViewFunctionSegment` supplies a borrowed root view without
copying trees or consulting source text. The existing envelope validation,
memory admission, replay, capture-position relocation and record retention checks
consume this view. A family is one envelope, never one independently shifted
segment per descendant; child positions and receipts remain in the shared owner.

The record-slice serializer supports the root's instruction-source table and
streams every family's member receipts and local positions. All member offsets
are relative to the segment's relocated `begin`, not to their individual root.
Template insertion captures remain forbidden inside a segment; native lexical
captures already represented in AST are not such insertions. Moving coordinates
does not permit changing the original lexical embedding.

The selected-method producer now prepares and emits a complete family. It checks
the actual encoded target, kind, length, placement and name against the selected
creation site, then calls `PreparedFamily::PrepareClass` and `FamilyEmission`.
It indexes function creations once per selected invocation and charges the
index before family preparation. Preparation and printing retain disjoint grants;
no refused selected method falls back to a legacy body. The enclosing record
still uses the legacy structural assembler and remains explicitly incomplete.

Prepared receipts retain the record identity from each original function input,
before that input is consumed; only the identity is stored at this point, not a
successful stage claim. Typed failure projection preserves nested resource/work
failures and original instruction PCs without parsing diagnostics. A parent
creation PC does not replace an available child instruction PC.

Production builds exited 0 after this integration. ABC-only inspection of
pasteboard 7559 now exercises the family emitter: [2638,2924), record 6897,
one member and 43 original instruction dispositions. Method 7586 retains
[2307,2464), record 6897 and 21 dispositions; its output bytes and receipt match
the standalone entry. The standalone 7559 entry lacks required context and
refuses, so it is not a source comparison baseline. Immersive 21082 still refuses
in enclosing record assembly at PC 159; its family draft is not record success.
All these diagnostic commands exit 1. Tests and generated JS remain unexecuted.

Transport specifications now instantiate both payload kinds, including replay,
capture insertion before the segment, zero budget, invalid envelopes and
200/201 retained occurrences. These specifications are uncompiled/unrun.

Joining old/new headers exposed their unrelated `ReceiverBinding` definitions.
The new value binding's ABI-entry marker is now `ReceiverEntryBinding`; the old
receiver-expression variant is unchanged. This is a type-name correction, not
a new receiver equivalence or constructor admission.

## Ordinary Expression Inspection

`--value-source` now routes an ordinary/arrow creation through `PrepareCreated`
and `FamilyEmission`, instead of unconditionally refusing after standalone body
preparation. It reports `fragment=function_expression` and
`required_strict_destination=true`: the text is only a fragment for a strict
enclosure, not an executable script. No strict directive is injected into the
body, and a null global context still refuses free global dependencies. Actual
creation identity and runtime naming remain attached to the output.

Class-record segments and ordinary expression fragments now share one streaming
member-receipt/position serializer. It accepts only the sealed emission owner,
retains whole-family coordinates, and does not infer origins from source text.

On preparation refusal, the entry receipt stays at Decoded; the projected
`family_failure` is reported separately. An error from a descendant cannot
prove a successful root stage or be labeled as a root instruction PC. After
successful preparation the real root TreeVerified receipt is available for
print-failure reporting. No failure produces partial source in the JSON report.

ABC-only inspection of pasteboard 7117 and 7477 now produces named arrow
expression fragments, each with five original instruction dispositions and
record 6897. 7449 continues to refuse preparation without output; its receipt
remains valid and does not claim TreeVerified. This does not replace the full
P3 family gate or establish cross-application execution equivalence. Added CLI
specifications are unrun while tests remain paused.

## Ordinary Record Creation Selection

`FunctionCreationSelection` identifies a specific indexed caller/PC occurrence.
`BuildRecordSource` accepts either this selection or a class-method selection,
never both. It checks same-record creator and target ownership, passes the
selection through bounded synchronous expansion, and requires the resulting
record to retain a sealed family segment for that exact site and input identity.

The typed leaf bridge validates the original creation operands against the site,
borrows the actual generated record's global binding context, and calls the same
`PrepareCreated` / `FamilyEmission` pipeline. A selected failure cannot call the
old body writer. Root incoming captures and unresolved module aliases remain
refused. Retention transports the complete family with a single outer offset.
Selected typed refusals survive legacy body-error compression through the
expansion invocation; no later enclosing writer attempt can erase them.

ABC-only record inspection now retains 7117 at [975,1018), creator 7089/PC 55,
and 7477 at [1139,1202), creator 7089/PC 102. Class method 7586 remains at
[2307,2464). These records still report legacy enclosure, incomplete source,
nonexecutable status and exit 1. This exercises real creation-context transport,
not whole-record or cross-application acceptance.

7449 does NOT become publishable just by supplying globals. Debugging the actual
record invocation identifies `FamilyTreeError::kModules` for member 1 (7089):
the child needs module aliases. The diagnostic family builder has metadata
aliases, but their own contract explicitly does not prove TDZ, cyclic
initialization, scope or executable publication. The production factory must
close these obligations before admitting the full family; passing metadata
through and deleting the guard would not be a correct fix.

Encoded-fixture specifications now cover actual selected creation, whole-family
retention, wrong PC/length, zero memory and cycle refusal. CLI specifications
compare real record segments to standalone expression bytes/receipts and require
7449 to refuse without legacy substitution. These tests remain uncompiled/unrun.
