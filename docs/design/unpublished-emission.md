# Unpublished source emission

## Observed refusal

After same-catch argument support, pasteboard Home entry 6100 reached its
complete class member expansion but refused at outer PC98 with a work-related
resource limit. At the class failure boundary, `CreationExpansion` had only
15,625 remaining work units; its scratch allowance had returned to 67,108,864
bytes. These are debugger observations of logical admission state, not RSS
measurements or allocator-limit compliance evidence.

Both the class owner and callable owner previously generated a dry pass and
then regenerated the result. Nested class/function emission repeated those
passes recursively, even when writing into a private SourcePrinter result that
would be discarded on failure. Work grants correctly remained charged, but
the redundant traversals prevented reaching later semantic work.

## Publication capability

The internal `UnpublishedOutput` marker certifies that the output owner will
discard its entire artifact when a descendant fails. Only the common printer's
`ClassBuffer`, `LeafBuffer` and the discard sink carry it. Capture support alone
is not this capability: arbitrary capture streams remain unmarked.

The class writer runs once into a marked stream. The callable writer does the
same only when it has no direct-constructor-write proof that may trigger a
resource fallback/retry. Retrying after a failed prefix would corrupt the
artifact, so that branch retains its preflight. Ordinary caller-provided
ostreams still preflight before publication. Existing lexical/branch writer
preflights are unchanged.

The single pass still performs signature, role, body, capture-domain/rebasing,
descriptor and source checks. A failure is not success with a partial body.
No output is published by the common printer until its full result succeeds.
The change does not add caching, change function identity, refund work, increase
budgets, remove recursion/cycle checks or implement an asynchronous scheduler.
It adds no body-sized storage: it uses already-owned private buffers.

## Current evidence

Both production CLIs compile, `git diff --check` passes, and read-only review
found no confirmed correctness defect. With unchanged budgets, Home entry
6100 passes PC98 class generation and reaches PC389 (DEFINEFUNC target 5684),
where capture/source admission still refuses. At entry to that site's buffer,
the root expansion retains 57,000,000 work units. This is not a throughput or
RSS benchmark. ProgressBar entry 9915 still refuses its unresolved nested async
body at class PC264; no semantic check was disabled to raise coverage.

The complete Home record remains unpublished. Deferred tests include direct
public ostream failure atomicity, marked-buffer discard, constructor retry,
capture-token rejection, recursive expansion, nested preflight counts and
resource/output failures. Tests, generated-JS execution and RSS remain paused.
