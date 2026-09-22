# Debug Scope Evidence

## Implemented

Bounded dual-stream line-program decoder, typed debug local lifetimes/source
positions and per-method structural JSON output. Signed register numbers and
debug type/signature strings are evidence, not inferred JS declarations/types.
Explicit END_LOCAL endpoints remain distinct from absent endpoints, including
an explicit zero endpoint. Unsupported RESTART_LOCAL is an explicit failure.
Terminal file/source references survive without a subsequent position row.

Dynamic compiler INVALID_LINE (-1, wire UINT32_MAX) is represented as an absent
position line, with a signed accumulator allowing later line deltas. The first
paired run exposed this sentinel and rejected all programs; a failing regression
was added before fixing the interpretation. That failed run is not coverage.

Malformed or resource-limited debug scopes are reported separately from valid
module structure; lazy strings are preflighted before starting scope JSON.
Input changes, read failures and output failures remain fatal to publication.
Embedded source text is not copied: only its entity reference is retained.

## Tests

- Debug warnings-as-errors build; complete suite 88/88, 27.31 s.
- ASan/UBSan debug_scopes_test and module_writer_test: 2/2, 0.37 s.
- Failing tests observed before decoder, writer integration, terminal-reference,
  missing-line and malformed-debug-string fixes.
- Dual operand streams, signed registers, extended locals, repeated lifetimes,
  special position math, missing/explicit-zero endpoints, malformed references,
  truncation, restart refusal, local-state violations, PC/line range checks,
  exact/short initial storage and event quotas.
- Both same-register reuse and distinct-register workloads at 200/201/8192.
  Ordered register index avoids reverse scanning the local history.
- Independent review found terminal-reference loss and lazy-string failure
  isolation issues. Both fixed, tested and re-reviewed; no outstanding findings.
- `git diff --check` clean.

## Paired Corpus

Final outputs: `build/structure-scopes-final/`. Five ABC inputs, 152 records,
2111/2111 debug programs parsed with zero scope failures, 8924 debug-local
records, 59306 position records and 2111 terminal file references. Compiler
internal bindings (e.g. 4funcObj/4newTarget) are included; this is not a count of
recovered source variables. TypeScript 5.9.3 declaration syntax, JSON, manifest
and path/count checks passed. No sidecar source was engine input.

External `/usr/bin/time` measurements with 1,000,000,000-byte CLI RLIMIT_AS:

| Input | Peak RSS (KiB) | Seconds |
| --- | ---: | ---: |
| Immersive/modules.abc | 8448 | 0.56 |
| MultiCommunityApplication/modules.abc | 8700 | 0.75 |
| pasteboard/modules.abc | 8320 | 0.16 |
| push modules.abc | 8448 | 0.17 |
| push widgets.abc | 8320 | 0.02 |

Timings overlap the regression run and are not isolated benchmarks. These are
paired-input structural-export measurements, not full-corpus/final-engine
memory certification. Static classification cache unchanged.

Still pending: instruction-boundary verification of debug PCs, binding identity
and source scope reconstruction, class fields/layout/ownership, globals and
source signatures. Function-body expansion remains deferred.
