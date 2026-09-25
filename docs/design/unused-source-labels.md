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
