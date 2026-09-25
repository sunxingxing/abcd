# Explicit Field Values

SourceOwnStore now carries a typed ScalarValue/StringExpression RHS, defaulting
to the original accumulator input. The original OwnWrite and buffered-object
origin remain separate and unchanged. VerifyOwnFields still checks the existing
data key and allocation provenance before emission. This representation does
not turn arbitrary property stores into source class fields.

FoldFieldValue extends the existing adjacent named-store fold to own stores.
It requires unchanged receiver/name/kind, unmerged origins, an accumulator
producer with normal completion and no language/exception effects, and exactly
one SSA use at the original store. Handler/phi consumers count as additional
uses. The store stays at its original PC, while the removed producer retains a
zero-width anchor. Strings remain constant-pool references; private register
inputs are observed through the same Value path as ordinary scalar operands.

EmptyArray is additionally admitted only under the explicit V2 source contract
excluding resource-failure ordering differences. Its original scalar category
must match; allocation crosses only private receiver/static-name evaluation,
not getters or setters, and occurs once before the write. Other allocation
expressions and user-code evaluations remain excluded. This condition must not
be copied to observable-resource profiles without a separate proof.

The pass adds constant work per adjacent pair to the existing block traversal.
The RHS variant has no dynamic storage; sizeof-based SourceStatement admission
accounts for AST size. No use counts, SSA definitions, graph indexes or copy
ownership certificates are rewritten. Printer, sequence-mode emission and
precise-slot census all consume the explicit RHS. No new dependency is added.

Production abc_semantic/abc_structure builds and ABC-only export
`build/pasteboard-field-values-01` succeeded. Six page records remove 45 lines /
585 bytes of field-value transport, producing 5,890 lines / 109,470 bytes in
total. PageA's message, offset and bottom fields now read their register values
directly. Empty-array inlining was not observed in this export and is not claimed
as corpus coverage. Eight incomplete records, two refused constructors at PC 14,
nine metadata-only records and partial maps with 195 methods / 9,745 spans
remain unchanged. Independent read-only review found no concrete issue.

Tests, JS execution and RSS remain paused. Deferred regressions include normal
and own stores, descriptor refusal, different original write kinds, extra
accumulator/handler consumers, fresh-array identity, resource-contract fallback,
throwing receivers/setters, captured registers, output budgets and PC anchors.
General object construction folding and full source equivalence remain open.

## Elided Copy Gaps

The field-value fold can now cross already-eliminated scalar slot-copy NoOps
within the same block. Every skipped node must have no merged origins, and its
original instruction must copy a register/accumulator with normal completion
and no normal/pre-throw/exception effect. Constants, allocations, guards and
non-scalar nodes do not qualify. The final field store must still read the
producer's exact SSA definition, not a reload or merely equivalent value.

Only emitted assignments start the gap scan, so skipped intervals are disjoint;
the total pass remains linear with constant scratch. Intermediate PC anchors
are unchanged. This extends proximity, not the allowed receiver/RHS effects,
resource contract, property descriptor proof or liveness assumptions.

Read-only instruction inspection found the motivating shape in Home method
6016: PC 30 creates the array, PC 32 is a dead temporary save, and PC 34 defines
the field. The earlier strict adjacency rule could not reach the field even
after the save was eliminated. Production builds and ABC-only
`build/pasteboard-array-field-values-01` now emit direct `params = []` writes
across all six pages. Source totals decrease by 103 lines / 1,339 bytes to
5,787 lines / 108,131 bytes. Eight incomplete records, two refused constructors
at PC 14, nine metadata-only records and 195 methods / 9,745 spans remain.

Independent read-only review found no concrete issue. Tests, JS execution and
RSS remain paused. Deferred regressions add long dead-copy gaps, live saves,
reload-definition changes, merged-origin anchors, allocation/guard separators,
handler observations, tight budgets and geometric scaling. Full native object
literal assembly and executable equivalence remain unfinished.
