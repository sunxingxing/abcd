# Emitted Method Self Bindings

WriteClassMethod optionally reports SelfBindingUse as Unknown, Unused or
Required. This synchronous output is valid only on successful emission. Every
FunctionWriter attempt resets it, including preflight and final output, so an
earlier precise attempt cannot license omissions after a conservative retry.

Only the existing complete bound-read audit and precise VM-slot metadata can
produce Unused. The method function-ABI register must not be emitted; a missing
function slot is also unused under this certificate. Bounds are checked before
indexing the usage vector. Admitted expression kinds have no separate hidden
current-function name emission. Unaudited identifier-only class consumers and
legacy body paths keep Unknown and therefore retain their self bindings.
Function/capture consumers now pass the separate audit in `closure-slot-audit.md`.

ClassWriter stores one admitted enum per ordinary buffered/attached member and
passes its stable address through synchronous method emission. It omits the
post-class MethodSelf declaration only for Unused. Required and Unknown retain
the original immutable capture. Existing class-key validation proves these
lookups are fresh own data properties; no getter effect is removed. The emitter
does not replace a required self binding with a mutable prototype lookup.

Class preparation now retains these results before publishing its prefix;
see `class-source-template.md`. The wrapper is omitted only when every member
is Unused. No source-text reference counting or repeated body decompilation
is used to make this decision.

Storage is O(M) bytes for M member-use enums, admitted before allocation and
charged by actual vector capacity. Usage checks are O(1) per method and class
selection is O(M). Existing method byte/work budgets still apply.

## Production Evidence

Production targets abc_semantic and abc_structure built successfully. ABC-only
export `build/pasteboard-self-bindings-01` totals 8,056 lines / 138,039 bytes,
compared with 8,092 / 140,305 in `build/pasteboard-module-receivers-02`. Across
all eight source records, 36 self declarations disappeared, including all four
Logger bindings. Statuses remain eight incomplete, two refused constructors at
PC 14 and nine metadata records; partial maps retain 195 methods / 9,745 spans.

Independent read-only review found no concrete correctness issue. Tests,
generated JS execution and RSS checks remain paused. Deferred regressions
include live self-slot uses, explicit function-object loads, captures/legacy
fallback, preflight resets, mixed member requirements, attached methods, async
methods and tight class budgets. Overall source equivalence remains unproven.
