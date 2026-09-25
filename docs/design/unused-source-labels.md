# Unreferenced Source Labels

The shared printer now builds a typed label-use index before allocating output
buffers. It follows the region tree and source statements, never generated JS
text. Native if headers consume their conditional break; native if arms consume
their final join jump. Ordinary blocks retain explicit and synthetic fallthrough
break references. Loop continue targets use a separate label namespace.

An unreferenced RegionLabel can omit both its name and braces only where its
scope contains no generated lexical declarations. The current admission excludes
record printers, staged argument regions and array-capture declarations for the
whole function. Locals remain declared at function entry; catch and native-if
scopes remain unchanged. Nested functions and class expressions keep their own
scopes. Internal scope-depth admission is retained even for omitted wrappers,
so this simplification cannot newly authorize module initialization or returns.

The index costs O(B) bytes, charged by actual vector capacity before other
printer allocations. Construction is O(I + B + R), with bounded region depth;
each block occurs once in an admitted region tree. No per-label subtree scan,
instruction motion, CFG/SSA mutation, or discarded effects are introduced.
Printer-generated PC positions naturally account for removed wrapper text.

## Evidence and Limits

Both production CLI targets built successfully. An ABC-only pasteboard export
to `build/pasteboard-label-uses-01` retains eight incomplete app sources, two
refused constructors and nine metadata records. CommonConstants.GetValue loses
three redundant wrappers around its native early-return branches. The combined
source changes from 9,659 lines / 163,631 bytes to 9,537 lines / 162,889 bytes.
Method position artifacts still contain 195 method entities / 9,745 spans.
These counts do not establish complete instruction coverage or equivalence.

Read-only review identified a budget-underflow risk when the index was prepared
after output had started. Preparing it before all output allocations resolves
that risk; the reviewer confirmed the revised ordering.

Tests, generated JS execution and RSS measurements remain paused by user
instruction. Deferred regressions: if/else join consumption, retained external
break targets, loop exits, try/handler scopes, staged-argument fallback,
array-capture declarations, record declarations, tight output budgets and
generated-position offsets. Artifacts remain explicitly incomplete and
non-executable; no end-to-end semantic-equivalence claim is made.

## Label Continuations

Ordinary RegionLabel nodes wrap the prefix immediately preceding their exit
block in a RegionSequence. The exception builder also wraps a try body with a
label targeting its verified skip-over-handler gap. That gap has no language
effects and prints only a zero-width anchor before the try closes; it is not
an ordinary sequence child. Both forms therefore admit their exit as the
body's natural continuation, supplied by the printer and label-use collector.
Existing continuation rules propagate only to a sequence's last child and
never through a loop or try region. Thus a final unconditional jump or synthetic
fallthrough to the label's own exit can be omitted; earlier exits and different
targets remain explicit. A now-unreferenced declaration-free wrapper may then
disappear through the existing label policy.

Methods with a cross-block argument owner retain the previous protocol: they
do not acquire a label-derived continuation. This prevents bypassing that
owner's interval consumption or creating a second anchor for its instructions.
Record and array-capture scope restrictions remain unchanged; removing a
redundant final jump does not license removing a lexical scope. Explicit jump
PCs retain zero-width anchors, while implicit fallthrough emits no extra text.
No graph, effects, source operands or exception dispatch is changed. Complexity
and storage remain those of the existing single region traversal.

Deferred cases include nested labels with different exits, early conditional
breaks, prefixes before try blocks, loop entry/backedges, argument intervals,
scope-sensitive declarations and exact PC spans. Tests, JS execution and RSS
remain paused.

### Production Evidence

Both production CLI targets rebuilt successfully. The ABC-only export at
`build/pasteboard-label-continuations-final-20260925` contains 19 records and
219 methods. Publication remains nine metadata-only records, two refused
constructors (entities 4767 and 5246 at PC 14), and eight incomplete sources.
The latter total 3,597 lines / 83,681 bytes, compared with 3,702 / 84,688 in
`build/pasteboard-branch-continuations-20260925`. Position artifacts retain
195 distinct methods and 9,745 spans. Counts alone do not prove span accuracy
or semantic equivalence.

Inspection of Home's emitted diff confirms that the conditional early exit
inside onPageShow remains while its trailing jump and wrappers before try
blocks disappear. The exception builder verifies skip-gap effects and forbids
outside entries; the printer preserves the gap's zero-width anchor inside try.
Read-only review found no concrete correctness defect and prompted the
distinction above between ordinary prefix labels and try-gap labels.
