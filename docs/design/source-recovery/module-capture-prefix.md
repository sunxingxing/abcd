# Qualified module capture initialization

A descriptive `module_prefix_initializations` entry is not a capture premise.
`ModuleCapturePrefix` qualifies it only for a same-file ordinary synchronous
record entry, under a contract excluding VM resource failures. Shared/TLA
contexts, V1 and sendable/concurrent entry kinds do not qualify.

The qualifier reuses the metadata local-slot resolver and checks every store
against its actual adjacent-name slot count. It resolves function targets in
the original caller pool and rejects sendable, concurrent, foreign-owner and
non-DEFINEFUNC creation paths. A prefix census separately rejects instructions
outside the allowed pure-copy/function-creation/lexical-store/module-store
window. The original allocation, matching cell store and single-block extent
are checked before a candidate is retained.

Pinned ModuleValueAccessor::StoreModuleValueInternal enters
SourceTextModule::StoreModuleValue directly. For a valid ordinary module slot,
this is internal array storage and possible allocation, not a user setter.
Shared-module non-shared exports can throw SyntaxError and are excluded here.
The ordinary record-entry restriction also avoids sendable-function module
resolution. Resource exclusion is required for allocation failure after a value
has been published. This is not a blanket permission to cross publications.

The owned proof is tied to immutable input identity/method and move-stable
instruction/cell arenas. Capture binding and its verifier receive the qualified
view; copied or different input arenas do not inherit it. Rebase still checks
the exact creation/allocation/store ordering and only discharges Language reads
under transitive Language-write validation. Defined-domain and phased capture
rules are unchanged. Parent and child source/capture checks remain mandatory.

Metadata resolution uses the existing indexed resolver. One instruction prefix
count and indexed store/block checks avoid per-cell instruction scans. Retained
proof storage, metadata scratch and work are bounded by the family budget.

Tests cover successful delayed capture, intervening user calls, V1, bad slots,
different input instances, work limits and three profiles. Independent module
execution verifies the eventual captured value and identity. Actual HotListItemView
now passes the former capture failure and reaches a separate FunctionAbiError
kLength at method 48015; neither that record nor stage one is complete.
