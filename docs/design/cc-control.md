# Claude Code Task Control

Status: operational design approved by Codex on 2026-09-26 under the user's
request for automatic GLM execution with Codex design and code gates.
The controller is external to the C++ decompiler and must not enter engine
dependencies. Implementation/code review uses GLM/Codex respectively.

## Gates

For each task in the existing plan, in dependency order:

1. Freeze current clean HEAD and task contract. Codex reviews the design in a
   read-only session; its structured decision is bound to that HEAD and task.
2. Only an approved design may start Claude Code. Pass the bounded task and
   approval as instructions. The worker cannot advance to another T.
3. The worker commits changes and reports completion or a real blocker.
4. Freeze its clean final HEAD. Codex reviews the exact base-to-head range,
   related code, and task contract in a fresh read-only session.
5. A positive, well-formed decision with the matching task and HEAD opens the
   next task. Any correction invalidates the previous code approval.
6. Request changes returns a scoped, design-approved correction to Claude Code;
   after at most three code-review/fix rounds, pause with preserved commits.

For the initial T00, the review and corrective design are recorded in
`docs/reviews/2026-09-26-t00-0dd813b.md`. The current process performs that Codex
review; the controller must not incorrectly seed an approval of 0dd813b.
New defects needing a materially different design require another design gate.
Unknown/malformed decisions, missing results, dirty review state, changed HEAD,
or audit process errors fail closed. An error never means approval.

## Credentials and Quotas

Read `~/.claude/settings.json.yl` and `~/.claude/settings.json.max`; never rewrite
the user's global settings. Allow only the expected HTTPS provider origin.
Inject credentials in the subprocess environment, not command arguments or
prompts. Do not pass GLM credentials to Codex. Filter unrelated auth, MCP,
plugins, status commands, and API fallbacks out of worker launches. Review
processes do not inherit GLM settings or call other model providers.

Use the official usage-monitor endpoint without a model call. Prefer yl while
its applicable model window is available. Max requires BOTH its five-hour and
weekly windows to be available. Parse type/unit/number from raw quota data;
the installed display plugin's generic five-hour label is insufficient.
Unknown/missing windows or query errors are not proof of exhaustion or capacity.
No guessed five-hour timer; use provider reset timestamps where available.
Respect provider subscription terms and each account's actual limits.

Only confirmed exhaustion permits fallback from yl to max; authentication,
network and generic throttling errors must not be treated as consumed quota.
After exhaustion, stop at a safe boundary, preserve local files/session, and
continue the same task with a new process using the other account. A reset yl
is preferred at the next phase boundary. Both exhausted means sleep/recheck,
not model polling. Re-query at phase boundaries and at bounded intervals while
GLM is active; no Codex calls for monitoring or waiting.

## Process and State Ownership

One controller lock and one code-writing process at a time. Do not run alongside
a manually started developer in this workspace. Bound subprocess duration,
logs and repair attempts. Record task, phase, base/head, session IDs, review
decisions and safe quota snapshots atomically outside the project. Never log
keys. Private control directories use owner-only permissions.

Use explicit session IDs for interrupted Claude work, never ambiguous --continue.
On recovery inspect git state before replaying actions. Do not reset/clean user
files. Unexpected external edits stop the controller. A crash or stopped service
preserves checkpoints and requires state reconciliation before restart; do not
claim transparent exactly-once execution of shell commands.

Codex review is read-only; the worker writes only the project task. Controller
approval records are not supplied as editable worker deliverables. This is an
automation boundary for trusted local tools, not a security sandbox against a
malicious process running under the same Unix account.

## Verification Policy and Costs

Tests, generated-JS execution, benchmarks and RSS tracking remain paused.
Production compilation and ABC-only inspection are permitted. A reviewer may
approve code progression with those explicit verification gaps, but must not
declare the corresponding semantic-validation gates passed. No task may expand
host assumptions, read paired sources or sourcesMap.map, or bypass old failures.

GLM implementation and waiting do not invoke Codex. Codex is invoked ONLY for
design review and post-task/fix code review, which necessarily consumes Codex
tokens under the user's newer requirement. Bound logs and return concise
structured findings. There is no promise of zero total Codex usage.

The first automatic run is bounded to T00 corrections followed by T01--T12 in
the documented dependency order. At that boundary report remaining Cnn work;
do not generate an unbounded series of new paid tasks without a reviewed scope.
