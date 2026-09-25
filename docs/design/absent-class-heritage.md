# Absent Class Heritage

The pinned dynamic class runtime distinguishes Hole (no extends clause) from
null (derived class with null heritage). The shared verifier previously
required Language for every register read, rejecting CommonConstants/Logger
at the STA following LDHOLE, before class ownership could be checked.

Normal source verification now permits an exact ValueKinds::Hole read only
for a normal pure slot copy or the matching ClassCreation parent register.
All other non-language reads still fail. In particular, return, call, lexical
store, property write, comparison and mixed Hole/Language phi values are not
admitted. This is internal marker transport, not conversion to undefined.
It adds constant work to the existing per-read verification, with no new
graph walks, tables or retained allocation.

The shared AST erases LDHOLE and pure assignments whose proven write is
exactly Hole. Every observable use must already have passed owner verification.
A class parent proven exactly Hole becomes typed HeritageKind::kAbsent in
ClassExpression. The assembled class adapter passes absent parent to the
existing class writer, which checks its matching heritage certificate and
prints native class syntax without extends. Language parents retain the bound
register path; null is not treated as absent. Exception input admission is
unchanged and does not gain a new Hole exemption.

Both production CLIs build. ABC-only export in
`build/pasteboard-shared-heritage-01` now contains Home and Logger shared-source
artifacts. Logger is emitted at
`sources/entry/src/main/ets/common/Logger.incomplete.mjs`, with 21 entry PC spans
and explicit incomplete/non-executable metadata. CommonConstants advances from
PC7 input rejection to PC43 unsupported static-initializer invocation; it is
not published. No sample names or IDs participate in production dispatch.

Read-only review found no concrete defect. Tests, generated-JS execution and
RSS collection remain paused; this is not equivalence or memory-compliance
evidence. Deferred regressions include direct/copy/phi absent parents, mixed
parents, null heritage, overwritten slots, internal-marker escape via each
observable operation, dead Hole loads, duplicate copies and exception owners.
