# Readable statement projections (2026-10-21)

Baseline: `40ed813b`. PrinterPolicy::kReadableV1; no language-contract change.
This plan changes source text after recovery has verified the unchanged tree.
It grants no engine ownership, capture, initialization or exception permission.

## Certificate and emission

`readable_statements.{h,cc}` builds statement-indexed decisions after the
binding/nullish/loop projections. `VerifyReadableStatements` recomputes them
from the immutable tree, OriginalFlow and those verified projections. Both
counting and real printing use the same prepared SourceVariables. Suppressed
statements and empty arms retain zero-width boundaries, child origins and
anchors through ElideStatement. Header operands get their actual new spans.

The passes are arena censuses plus an explicit-stack scope traversal: linear
in owned statements, expressions, symbols, case tests and labels. Each switch
case list is inspected a constant number of times. There are no per-binding
CFG walks, recursive walks or effect guesses based on printed names/opcodes.
Every decision/scratch/stack vector is prepaid by TreeCheckBudget; exhaustion
remains the existing typed resource/work failure. Synthetic widths
200/201/400/800 test exact bytes/work and one-less refusal.

### TDZ reads

A direct discarded SymbolExpr must be backed by either the checked
OriginalActionRegion for that read/HoleGuard or OriginalInitializedGuard
and its exact OriginalCells reference/result identity. The latter is important:
initialization proofs can make the raw guard a nonexecuting anchor, while the
source still contains a redundant standalone binding read.

The next source statement must write the same projected binding/storage.
Lazy module reads are excluded: they can execute dependency code.
Plain assignment requires the complete projected RHS evaluation to be Inert:
no hook, write, allocation, suspension or possible throw. The summary comes
from the existing binding/evaluation machinery, including all child events.
This preserves the ordering of a failing TDZ check: GetValue used to throw
before the RHS; native assignment PutValue can throw only after that RHS.
An unknown/effectful RHS remains guarded. Native binding update has GetValue
as its first action, before ToNumeric, so its guard does not move. This AST
has no separate native binding-compound-assignment node; any such future
projection must supply its native Get/RHS/PutValue event correspondence.

The check uses SymbolId/storage identity, not equal source spellings. It
excludes declaration conversions, literal/read substitutions, residual value
projections and loop relocation. It does not erase arbitrary unused reads.

### Empty branches

A removable arm is a textless empty BlockStmt (or contains only independently
omitted/inert invocation reads), with no symbols, label, capture-scope identity
or other print projection. Dropping an empty else retains the condition and
consequent. An empty consequent becomes the original alternate under inverted
condition polarity. BooleanOperand performs the existing readable negation:
no doubled NOT, exact equality complements, no relational complement over NaN.
If the alternate was printed as `else if`, its retained real block is braced
again; no dangling-else pairing changes. No nonempty body loses its braces.
The normal/abrupt completion is identical, including UpdateEmpty behavior.

### Switch

One explicit-stack traversal records the innermost breakable statement.
A labelled break becomes plain only when its target is that exact innermost
SwitchStmt, and all uses of the switch label can be removed. Nested loops,
outer breaks/continues, or any remaining use retain the label. Case order,
fall-through and case/body scopes stay unchanged.

A separately adjacent switch snapshot can move into the header only when
its private, noncaptured compiler binding has exactly one definition, all its
reads are the switch's verified discriminant-test reads, projected storage
identity is unchanged and no other print projection intervenes. Direct
function/class RHS values stay excluded to preserve NamedEvaluation. The
switch's case comparisons keep boundary positions; the RHS is printed once
in the header. Unit tests cover the positive and extra-use/capture negatives.
The common SourceCells analysis also covers class/member captures and unknown
capabilities; a missing/external capability cannot count as private.
No separate cohort snapshot met this obligation. The native switch already
evaluates its discriminant with an assignment in the header
(`switch ($value3 = message.type)`). Removing that embedded cache plus its
distant empty declaration is outside the adjacent-definition projection and
remains conservative; the switch-label cleanup does not claim to remove it.

## Constructor/callee and selection snapshots: deferred design

No constructor/callee/selection snapshot is erased in this round. In particular
TimeUtils's retained DayInfo and phi assignments remain intact. Their raw
certificates prove an event forest; SourceVariables does not currently have a
joint certificate for removing its private writes and moving the surviving
reads through the final printed invocation projection.

A following increment should derive a NativeInvocationProjection from the
verified field/constructor/reference views. It must identify the one callee
read, immutable import/const storage (ordinary loading, no lazy evaluation),
or prove absence of reachable writes through every argument event. It must
also prove that each selection snapshot has exactly one surviving argument
use, conditional execution, original argument order, name/GetValue category,
same catch exit, and no capture/live-out. Mutable/unknown bindings and a
selection reused after the call retain their snapshots. Verification should
reconstruct the removed assignments and each native invocation port rather
than authorizing a generic comma-expression simplifier. No new premise is
needed, but that new joint projection is outside this slice.

## Acceptance tool

`grouping_bytecode.py --readable-statements --changed-only` retains strict mode
as the default. The opt-in mode uses Acorn to derive only these OLD-source
statement edits; all remaining tokens must be grouping/whitespace equivalent.
Wrong predicates, changed operands/effects, nonempty deleted arms and a
switch break targeting through an inner loop are rejected. AST matching keeps
ASI and if/else pairing; the real lexer also retains literal/comment spellings.

When OLD and NEW assembly differs, the tool compiles a third OLD witness
containing exactly those independently derived edits. NEW must match that
whole disassembly byte for byte, including registers, branches, handlers,
literals, annotations and module metadata. No assembly normalization is used.
Reports list every nonidentical method and its approved source-rewrite classes;
these are allowed rewrites, never reported as identical OLD/NEW bytecode.

The existing identifier-valued string export adaptation is applied identically.
One changed .ets file has Ark `import lazy` syntax. A real-lexer shadow blanks
only that modifier for Node/Acorn, retaining its metadata and character offsets;
es2abc receives the unchanged lazy imports. Node syntax on that shadow does
not verify lazy-import runtime semantics.

## Cohort evidence

OLD and NEW preserve the complete sets: previous flags 541/620 and source
599/620; no new completion or regression. Scratch evidence is under
`build/review/readable3/`.

| Rewrite | Source occurrences / files | Previous occurrences / files |
|---|---:|---:|
| Certified guard read removed | 106 / 43 | 61 / 29 |
| Empty else removed | 29 / 22 | 25 / 19 |
| Empty consequent inverted | 201 / 68 | 118 / 46 |
| Plain switch break | 7 / 1 | 0 / 0 |
| Unused switch label | 1 / 1 | 0 / 0 |
| Switch discriminant snapshot | 0 / 0 | 0 / 0 |

There are 142 adjacent bare-read/store sites in 46 complete source files in
this baseline (the earlier estimate was 128/45). 106 disappear; 36 stay.
The remaining printed sites are residual/composed read occurrences rather than
this plan's directly certified SymbolExpr statement/store pair. Matching their
printed names is insufficient to extend the certificate. Examples are
bufferSize/readDataCallback/file and the guarded late-store `started` /
`isPlaying` / `isDucked` paths. Their projection/catch/effect evidence must be
bound at the final source occurrence before removal; this round keeps them.
The census and a test-only instrumented copy of the plan live in scratch only.

107 source files and 83 previous-flags files change (categories overlap).
Token checks: 107/107 and 83/83. Node syntax: 107/107 and 83/83, with the one
Ark lazy-import shadow noted above. At default optimization, SDK comparisons
are source 12 identical / 95 allowed / 0 differ / 0 unverified; previous
12 identical / 71 allowed / 0 differ / 0 unverified. At O0 they are source
0 identical / 107 allowed / 0 differ / 0 unverified, previous
0 identical / 83 allowed / 0 differ / 0 unverified. Nonidentical method bodies
are all exact matches to the compiled source-edit witness: source 204 default
and 222 O0; previous 118 default and 135 O0.


## Representative source changes

```js
// Before                         // After
searchResult;                     // (guard represented by the native write)
searchResult = data;               searchResult = data;

if (audioRenderer === void 0) {    if (audioRenderer !== void 0) {
} else {                            audioRenderer.on("writeData", writeDataCallback);
  audioRenderer.on("writeData", writeDataCallback);
}                                 }

if (!message.fileUri) {            if (message.fileUri) {
} else {                            loadCA(message.fileUri);
  loadCA(message.fileUri);         }
}

$loop0: switch ($value3 = message.type) { // switch ($value3 = message.type) {
  // ...                                // ...
  break $loop0;                         break;
}
```

The complete before/after diffs for all 107 source files are in the scratch
`examples.json`. Paired samples keep 80 complete records; two files change,
with three empty-arm inversions. Token and Node checks pass 2/2; at each SDK
optimization they are 0 identical / 2 approved / 0 differ / 0 unverified.

Full build passes; 65/65 serial source-regression; 3/3 new/extended readable
tests. The requested sweep is 147/159 with twelve OLD failures reproduced.
Only `tests/integration/binding_initializer.mjs` needs a changed existing
spelling expectation: the abrupt completion now lives in an inverted
consequent. It asserts the exact new predicate and ReturnStatement, removes
that return, parses the mutant and still observes the behavioral mismatch.
