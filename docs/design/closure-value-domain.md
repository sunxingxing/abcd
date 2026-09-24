# Closure writes and local value admission

## Conditional invariant

`ClosureWrites::kLanguage` is an explicit callable-owner premise for the common
normal-CFG source path. It says that every successfully emitted descendant
stores only Language values into captured cells. It does not say that a child
is read-only, that a cell retains a particular object, or that undefined is
excluded. The default `kUnknown` retains the original creation barrier.

The invariant has an initialization base and checked transitions:

- Local allocation and a dominating initial store remain mandatory. Capturing
  an uninitialized cell is still rejected at the creation site.
- Every parent store still passes register SSA Language validation.
- Generic children do not admit captured stores. The existing linear child
  path checks each `CapturedStore` input with `IsLanguage()` before emitting it.
  Its typed capture tokens are rebound to the actual parent's cells.
- Descendant templates repeat these checks. Recursive expansion still has its
  existing cycle/depth/resource refusal; this is not an optimistic cycle proof.
- Every function creation remains in the AST. The common body owner returns
  an artifact only after complete printing and child validation. A failed child
  invalidates the whole artifact, including previously printed in-memory text.

Under that premise, initialized local loads may continue after a creation, and
later children may bind the same initialized cells. Source loads remain reads
of mutable lexical variables. They are not replaced with creation-time values.
The premise is stored in `SemanticProof`, so normal capture rebasing uses the
same contract as local admission.

## Boundaries

Only `BuildSemanticBody` / `BuildSemanticTemplate` opt in, as owners of complete
child emission. Detached analysis retains `kUnknown`. The phased exception path
is unchanged. No serialized labels, opcode strings, or generated source text
participate in the decision.

Home constructor 5851 reads its local this cell after creating child 6322 at
PC398. That child itself creates multiple callbacks. The prior unconditional
creation barrier was an obstacle to the common path independently of whether
these callbacks preserve object identity. This change removes the value-domain
obstacle only; Home's range-super proof still needs lexical reaching-value and
closure-write identity analysis. It must not use this premise to erase super
checks. Writes of undefined are Language writes and can invalidate those checks.

There are no additional graph walks, allocations, caches or dependencies.
The existing initialization and capture lookups retain their complexity; the
added policy checks are constant-time and the proof carries one enum.

## Evidence and deferred verification

Both CLI targets build. ABC-only production inspection produced
`build/pasteboard-source-closure-domain/manifest.json` and
`build/pasteboard-semantic-closure-domain.jsonl`; they are byte-identical to the
preceding range-super checkpoint's corresponding artifacts. No additional
complete record or semantic equivalence is claimed. Tests and RSS measurement
remain paused by user request.

When verification resumes, cover initialized cells after sibling creations,
Language-only child mutation, undefined writes followed by super checks,
uninitialized captures, failed descendant templates, internal-value stores,
unknown-policy refusal, and parent output rollback. Include normal and
template owners; exception admission must remain unchanged.
