# ABC Decompiler Rules

Applies to code, tests, tools, documentation, and agents. Implementation is
authorized. Linked specialist contracts remain binding.

## 0. Goal and Reference
- Current priority: recover record paths/file names, module imports/exports,
  class layouts/fields, globals/locals, function names (including async) and
  signatures before expanding function-body decompilation. Publish structural
  artifacts as explicitly incomplete; never fake executable bodies or confuse
  record metadata fields/register slots with source class fields/variables.
- Current gate: finish instance fields, global variables, complete source
  signatures and function-body integration before resuming broader corpus
  completion work. Evidence-only metadata is not completion of this gate.
- C++20; semantically faithful, analyzable JS/TS/ArkTS, one file per record.
  Assembly, fake bodies, and copying sidecar source are not successful
  decompilation. Keep evaluation source outside engine inputs.
- Reference official `OpenHarmony-v7.0-Release` and identities pinned in
  `docs/reference/openharmony-v7.0-release.lock.json`; retain historical ABC
  support through explicit container, ISA, and semantic profiles.
- Prefer proven industry algorithms and implementations. Document provenance,
  prerequisites, complexity, licensing, maintenance, and measured tradeoffs;
  pin dependencies. Never trade correctness for speed or apparent coverage.

## 1. Names and Types
- Use short, meaningful, consistent domain names; avoid vague catch-all names
  (Manager, Helper, Utils, Common, Data, Process) and invented abbreviations.
- Use enums, strong IDs, class fields, typed records, and variants for semantics;
  handle alternatives exhaustively. No semantic string tags, string-keyed bags,
  or dispatch on opcode names, diagnostic text, printed IR, or source fragments.
- Strings are restricted to logs and necessary textual data/boundaries: generated
  source, names, literals, module specifiers, paths, serialized labels. Validate
  explicit boundary mappings; internal semantics remain typed.
- No magic numbers: name/type constants or generate specification tables for
  widths, opcodes, flags, versions, ABI slots, sentinels, and limits; cite their
  source. Prefer typed absence. `kPairwiseLimit = 200` does not justify
  selecting a quadratic production algorithm.

## 2. Performance
- Entire decompiler process peak memory must not exceed 1,000,000,000 bytes
  (conservative interpretation of 1G), including all workers, file mappings,
  caches, IR, dependencies, and output buffers. Bound admission and memory;
  stream or spill when needed, report typed resource exhaustion rather than
  silently changing semantics. Verify peak RSS on full-corpus runs; allocator
  counters alone do not prove compliance. Aggregate child processes if used.
- Prioritize performance within correctness; define input/output dimensions and
  measure latency, allocations, and peak memory. Above 200 items, avoid O(n^2)
  or worse, including hidden repeated scans, copying, and per-variable graph walks.
- Prefer indexes, sorting, adjacency lists, sparse data flow, bounded worklists.
  Account honestly for dense inputs/output; use compact representations and
  resource limits, never discarded semantics. Small slow test oracles are OK.
- Test 200/201 and geometric scaling; follow `docs/design/algorithms.md`.

## 3. Function Tasks
- Default asynchronous, parallel, lazy function decompilation: proven bounded
  executor, worker/memory budgets, cancellation, backpressure. No thread or
  unbounded std::async per method, or blocking on same-pool queued dependencies.
- Deduplicate in-flight work, isolate mutable method state, publish immutable
  results, preserve file lifetime, schedule dependencies and handle cycles.
  Confine unsafe upstream readers without eagerly loading every method body.
- Keys include file identity, profile, stage, rules/engine revisions, and options.
  Follow `docs/design/method-tasks.md` for cancellation, eviction, failure and
  ownership contracts. Sync/single-worker modes wrap this system; engine
  concurrency is distinct from recovered JS async semantics.

## 4. Maintainability and Tests
- Use concise Prompt-in-Code comments at semantic, algorithm, version, ownership
  and concurrency boundaries: intent, invariants, forbidden changes, provenance,
  regression tests. Keep comments current; no trivial narration or boilerplate.
- Split files by coherent responsibility, not arbitrary line counts. Small public
  headers; isolated upstream adapters; generated files in build directories.
- Production in include/, src/, tools/; tests, fixtures, goldens, mocks, fuzzing,
  benchmarks in tests/, separated by domain/type with CMake/CTest targets.
  No production dependencies on test helpers or test-only public APIs.
  Keep build/test outputs untracked. Verify before claiming success.

## 5. Corpus Validation
- Include paired and batch corpora; actual local roots are currently
  /home/rabbit/sample/abc_with_code and /home/rabbit/sample/abc.
- Static ABC is temporarily unsupported. Persist classification, content hash,
  detector revision and evidence in `tests/corpus/abc-index.json`; reuse valid
  static classifications without repeated detection. Never modify input ABCs.
- Unknown versions, malformed files and decode failures are not static evidence.
  Separate classification from support; use typed skip reasons; invalidate by
  content/relevant rule changes, not path alone. Follow `tests/corpus/README.md`
  for bounded hashing, atomic persistence, concurrent access and reporting.

## 6. Agents and Git
- After each coherent task commit, refresh `docs/design/source-recovery/status.md`
  with implementation, build, verification and remaining-gate evidence.
  Periodic design audits are cancelled at the user's request. Report concrete
  architecture risks when encountered; do not schedule audits by commit count.
  Component completion never substitutes for end-to-end acceptance. Respect
  paused tests/RSS; reporting does not authorize resuming them.
- Choose agent models by difficulty: economical for bounded mechanical work,
  standard coding models for integration, stronger models for complex semantics
  and review. Give bounded ownership and sufficient context.
- Promptly commit coherent, verified changes; do not accumulate uncommitted code.
  Preserve unrelated user changes; never invent verification results.
- At most eight worktrees including the primary checkout. Reuse isolation;
  promptly clean finished worktrees only after checking cleanliness and preserving
  commits. Never force-delete unfinished work.
