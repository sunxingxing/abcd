# Shared Class Initializers

CommonConstants entry 4329 creates a method on its fresh class and invokes it
with CALLTHIS0 at PC43. The shared receiver-call lowering previously required
a property-read Reference, so it could not represent this initializer even
though the legacy record writer already had an independent field-body proof.

The shared AST now retains a typed, unprintable SourceInitializerCall for a
normal zero-argument receiver call whose scalar-copy origin is a zero-length
method creation. This is a candidate, not admission. The existing class
assembly pass must establish the exact same static HomeObject and receiver,
one consumption, no escaping method values/copies or phi uses, no prior member
installation, and a same-block unobserved interval after class creation. The
scan stops immediately at the initializer; later effects never move ahead of
it. Unmatched candidates still fail at the printer.

The class adapter resolves the actual method target, checks non-sendable
ordinary kind and record ownership, and calls RecoverStaticFields. That proof
requires the initializer ABI, supported field-building effects and an undefined
return; no method body is guessed from its name. Recovered fields enter the
existing class writer, retaining its field/member ordering restrictions.
The original method/copy nodes become NoOperation and the call writes undefined
only after the body proof succeeds. Their PC origins map to the generated class
span. Exact constructor/null/absent-parent checks remain independent.

Dependency matching uses the existing copy-origin index and assembly facts,
with O(I + V + reads + phi inputs) scanning and bounded storage, not repeated
suffix or alias walks. Field recovery retains its existing algorithm/budget;
its returned allocation is charged while class emission is live. No new
runtime wrapper, mutable `.call`/`.apply` lookup or sample-ID dispatch is added.

Both production CLIs build and read-only review found no concrete defect.
ABC-only export in `build/pasteboard-shared-initializers-01` now includes
CommonConstants, Logger and Home. CommonConstants has 31 entry PC spans and
native static FULL_PERCENT, TEXTAREA_HEIGHT and BREAK_POINT_SM fields; all
artifacts remain explicitly incomplete/non-executable. Seven application
records still refuse and nine metadata records have no module.

Tests, generated-JS execution and RSS collection remain paused. Deferred cases:
wrong receiver/home, prototype invocation, repeated call, escaping function
copies/phis, nonzero length/arguments, effect barriers, cross-block calls,
non-undefined return, unsupported initializer body, duplicate fields, mixed
member ordering and resource/output failures. Full equivalence, style recovery
and nested-method PC positions remain unverified.
