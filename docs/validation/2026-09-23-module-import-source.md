# Module import source boundary (2026-09-23)

Pinned v7.0 `LDEXTERNALMODULEVAR` indexes the module environment built from
regular imports followed by namespace imports. The source writer now accepts
an explicit same-record `ModuleInfo`, checks the slot and request index, and
emits a read of the corresponding live ESM binding. The record prelude writer
emits request-only imports first to retain dependency evaluation order, then
slot-indexed named or namespace declarations. It stages bounded output so a
semantic failure does not publish a partial prelude. Created functions and
class methods propagate the context only when target ownership matches the
caller record.

Only an immediately adjacent accumulator `THROW_UNDEFINEDIFHOLEWITHNAME`
guard is admitted. Its name pool is not resolved during normal-path source
emission: pinned v7.0 resolves it only if the value is Hole. Generated ESM
reads reproduce live binding and TDZ behavior, but `$importN` aliases do not
preserve VM-specific ReferenceError message text. This remains an explicit
diagnostic-fidelity gap, not evidence of complete source recovery.

Synthetic tests cover missing context, invalid slots/requests, unguarded
loads, deferred guard-name resolution, namespace declarations, and 200/201/
400/800-site linear validation work. The Node oracle confirms a read observes
updates to an imported `let` binding. Full CTest: 111/111 passed. No record
source exporter calls this prelude yet; real corpus readiness and whole-process
peak RSS were not remeasured for this change. The current structural artifact
and function fragments must not be presented as a complete recovered module.
