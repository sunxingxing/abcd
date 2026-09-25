# Native Arguments From Nested Call Owners

The common call AST now carries a typed CallArgumentLayout certificate. kStaged
is the default; kOrdered admits native arguments with retained private result
commits, while kValues certifies that every immediate argument result commit has
been eliminated. A nested owner qualifies as one outer argument only with
kValues and a CallResultStore identifying its original result save. It cannot
return from the function or discard its result.

This distinction is necessary: blindly moving a nested call into an earlier
native argument could read an outer argument before an internal register write
that originally changed it. kValues excludes those internal writes, recursively.
Distinct increasing outer argument targets retain the existing non-overwrite
proof. Invocation, getter, Proxy, callback and coercion effects are not pure and
remain in the original left-to-right order, under the live outer Reference.
Private VM slots are not externally writable through JS or host callbacks.

FoldArgumentExpressions visits owners in reverse flat-AST order, so children
are certified before parents. OrderedArgument inspects an immediate assignment
or a certified child and jumps to the child's invocation endpoint; it does not
walk that child's arguments again. HasOrderedArguments preserves distinct
ordered targets and exact argument count. Existing SSA suffix reads identify
each actual argument without scanning all reads per value. For a nested result,
RetargetedOutput resolves the invocation's adjacent saved SSA definition, and
ReachesArgument proves that definition reaches only the outer argument through
already-elided identity transports. Only then is the nested result ArgumentOnly.

The unified printer accepts such owners in argument form, emits their normal
Reference and argument logic without a statement terminator, and skips their
already-emitted interior. The removed save is still emitted once as its original
previous_origin anchor. Source, invocation and save PCs therefore derive their
positions from AST emission, never from parsing generated JS. Stored results
remain valid assignment expressions when not sole-use. Argument-only results
are rejected outside argument form or without a kValues/stored-result certificate.

The proof adds no scratch allocation, dependency or execution-contract premise.
Inline layout storage remains covered by sizeof(SourceStatement) admission.
Well-nested intervals are inspected at their own immediate level, and certified
children are skipped in constant time: O(I log(I + U)) with existing indexed SSA
queries, not a recursive per-parent scan. The printer retains its existing
bounded call depth. Phased SSA, catches, branch-argument owners, intermediate
private writes and unproved saved results retain their existing paths.

Tests, generated JS execution and RSS checks remain paused. Deferred regressions
include getter/argument throws, private slot collisions across outer arguments,
multiple nested arguments/depth, retained result uses and phi observers, erased
identity transports, malformed intervals/certificates, slot census and nested
PC origins. This change is not proof of whole-application equivalence.

## Production Evidence

Both production tools built successfully and ABC-only export to
build/pasteboard-nested-call-arguments-20260925 succeeded. The six lifecycle
methods now emit `delete(this.id__())` without an argument IIFE. PasteBoard's
addEntry call accepts getString directly, and ProgressBar's createData call
accepts getUriFromPath directly, including its own non-empty argument list.
Totals are 4,204 lines / 90,313 bytes, down 32 lines / 348 bytes from
pasteboard-call-result-stores-20260925.

PageA method 7559 property PC 66, invocation PC 74 and saved-result PC 77 all
map to bytes [4037,4048), the emitted `this.id__()`. Outer invocation PC 81 maps
to [4023,4051), the complete delete statement. Partial maps retain 195 methods /
9,745 spans; record statuses remain eight incomplete, two host constructors
refused at PC 14 and nine metadata-only records. Read-only review found no
concrete defect. Tests, JS execution and RSS were not run.
