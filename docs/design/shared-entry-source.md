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

## Record Publication

`BuildRecordSource` is the production record owner for this pipeline. It
requires a same-file/same-record module context, an admitted dynamic profile,
ordinary merged ESM attributes, exactly one owned wire entry and a zero-user-
parameter ABI. It decodes the body, builds fresh export state and semantic IR,
then calls BuildEntrySource. Body capacities, export allowance and export-name
scratch are retained within its analysis admission. Metadata, lookup, roles and
module context remain caller-owned; printed output has a separate allowance.
This is not proof of measured whole-process memory use.

`abc_structure INPUT OUTPUT --shared-source` now publishes these artifacts
through the existing no-replace staged tree. It reuses ABC-derived record path
planning, writes `.incomplete.mjs` and streams `.entry-positions.json`. The
manifest uses `incomplete_source`, status 4, `source_complete: false` and
`executable: false`; it does not populate the legacy completed `source` field.
The position sidecar explicitly labels entry-only scope. The legacy `--source`
writer is unchanged. Shared mode does not call getrusage or report RSS; the
existing process address-space guard remains enabled, not a measured RSS claim.

Unsupported semantics produce typed per-record diagnostics without fake source.
Read/change/resource/output failures terminate export and discard the entire
staged tree, matching existing fatal publication policy. File identity is
checked before publishing. A conflicting destination is never overwritten.

ABC-only pasteboard export generated Home at
`sources/entry/src/main/ets/pages/Home.incomplete.mjs` with 150 entry spans.
All 19 records are represented in the manifest: one shared-source artifact,
nine application records with explicit refusal, and nine records without a
module. This does not replace the two records accepted by the legacy path or
claim corpus completion. No source maps or sidecar source were engine inputs.
Tests, generated-JS execution and RSS remain paused. Nested-method position
aggregation, source-style recovery and execution-equivalence verification
remain open, along with the explicit per-record refusal cases.
