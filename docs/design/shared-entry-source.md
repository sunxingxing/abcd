# Shared Entry Source Pipeline

`BuildEntrySource` moves the normal entry inspection pipeline out of
`abc_semantic` into the source library. Record, function and class-form entry
inspection now share capture preservation, source proof, region recovery,
named-scope validation, AST lowering and printing through that API. It consumes
the original body, semantic IR, ABI frame and typed owner context. It returns
the existing typed failure variants or `PrintedSource`, preserving its source
contract and PC spans. No JS text is parsed to reconstruct semantics.

The caller still owns profile, packaging, record-entry identity, module-table
ownership and ABI checks. The API is not a new complete-record certificate.
Record export state must be private to the attempt and discarded after failure.
The printer independently checks record inputs and exports before returning.
The legacy record writer remains unchanged until the publication owner can
explicitly distinguish a shared-source artifact from a complete record.

This is extraction of the already exercised pipeline, not a new admission or
performance policy. Its analysis budget includes the supplied live IR and
retained proofs; body, indexes and module context are caller-owned allowances.
Printing has its existing separate allowance. Work retains existing stage
bounds rather than claiming a total cumulative limit. Neither this accounting
nor compilation proves the whole-process memory requirement. SourceFailure
diagnostics now use the CLI print-stage category, including failures formerly
reported under AST or capture proof; the typed reason and PC remain available.

Both production CLIs build. ABC-only Home entry 6100 generation through the
new API succeeds with 48,935 source characters and 150 entry PC spans in
`build/pasteboard-home-shared-entry.json`. `source_complete` and `executable`
remain false. Read-only review found no extraction defect. Tests, generated-JS
execution and RSS collection remain paused.

Next integration is the staged record publisher using this API and existing
ABC-derived record path planning, without promoting incomplete artifacts to
complete source. Nested-method position aggregation, source-style recovery,
publication integration and execution-equivalence verification remain open.
