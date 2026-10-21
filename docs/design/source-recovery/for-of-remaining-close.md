# Remaining o32 iterator closes: nested entry boundary checkpoint

Latest: [Group B continuation](#group-b-continuation-async-body--synchronous-iterator-2026-10-21) completes23058; the older checkpoint below records baseline92e4bfcb.

Baseline: `92e4bfcb`. This is a **partial implementation**, not completion of the
three-record round. No new source-language premise or publication is introduced.
Paired source remains evaluation-only.

## Nested original-edge derivation

For m42717, the inner return getter is PC635 (Try0, H658) and the outer
getter is PC705 (Try1, H728). The old continuation walk refused at the outer
closed-state predicate PC692. Both loops already have acquisition, advance,
done/success and guard identities from SSA and the candidate scan.

`DeriveCloseEntries` now indexes guard-entry nodes once. When the eventual
completion crosses a strict enclosing protocol activation, it follows that
activation's proved guard bypass **only to discover the terminal completion**.
The inner relation is cut at the first enclosing guard's original Before node;
it does not absorb or execute the enclosing close. The enclosing occurrence's
state/reference proof is still independently required. No PC/layout adjacency
or source-text recognition establishes the boundary.

The actual bridge is **Commit PC685, successor0 -> Before PC687**. The inner
relation and outer derived entry both carry **Return(V232, caller)**. The outer
predicate is PC692, not the entry itself. A failed inner close still takes its
original exception dispatch into H760; it is never substituted with the original
Return. That dispatch and the subsequent current-Throw bridge have not yet been
certified as nested ownership, so `kNested` remains missing.

The next entry-derivation witness was PC427: an unreachable explicit rethrow
from an inactive handler, rather than an observable body exit. Entry enumeration
now excludes sources outside the original exception dominator tree's reachable
set. It does not remove any reachable throw/handler edge. Producer and verifier
rebuild reachability and guards independently.

All four m42717 closes now derive and verify their separate priority tables.
The method still fails ownership:

- Inner return close PC635: `kLiveOut` at PC753, reading register27. The close
  member set includes its register27 restore at PC685; the existing private-state
  proof treats that physical slot as private. The read must be accounted for as
  preserved completion transport (or proved dead by common value liveness), not
  accepted by ignoring private-state observations.
- Outer return close PC705: `kUserTry` / Try4. This is the inner loop's
  body-protection wrapper, which is not an ordinary user try. Its role must come
  from the independently sealed inner statement, not containment alone.
- Inner Throw close PC780: `kBodyDispatch` / Try4 at PC705.
- Outer Throw close PC843: `kBodyDispatch` / Try7 at PC480.

Thus entry identity and separate priority pass; shared storage, nested body
coverage/dispatch, and CURRENT-completion ownership do not. The blanket nested
admission gate is intentionally retained. No TreeFlow/recovery/publication gate
is lifted by this checkpoint.

## Sequential and await cases, unchanged

m47128's two loops remain independent acquisitions. The first close's entries,
ports/private state/region split verify. The second close still has
`kBodyDispatch` / Try6 at PC717, through the per-iteration environment cleanup.
Fresh captured per-iteration bindings and multi-acquisition tree/recovery
composition are not implemented by this change.

For normalized m23306, the located raw-flow witness remains **original PC99 /
working PC96**, `CloseEntryError::kCorrespondence > CompletionRelationError::kInput`.
The adapter must consume verified async resume/rejection correspondence; removing
that edge or treating the normalized await as synchronous would be unsound.

## Complexity and tests

The new guard index is one PMR vector over original exception nodes, charged by
the existing arena before allocation. Lookups are constant time; containment is
answered by the existing parent-linked loop intervals. Continuation walks and
relations use the method's prepaid O(N log N) work grant; dense queries refuse
resources instead of granting partial proofs. No per-acquisition whole-method
index is rebuilt.

`close_entries_nested_test` uses the existing real es2abc nested fixture (m395).
`close_entries_nested_fixture_test` uses the new `for_of_nested/main.abc` (m396):
callback throws and Return cross both loops. Its build.json pins compiler SHA,
commands and input/output hashes at O0/default. Tests check the exact normal
bridge, correlated payload/target, reverse verifier, changed/missing continuation,
wrong payload/target/entry, omitted nested close, and resource/work exhaustion.
They run 200/201/400/800 padding and register renumbering. The new default-opt
fixture still has an earlier ExceptionPlan/layout refusal at PC450; it is not
counted as a supported or differential pass.

There is no newly published method, so an engine-output Node differential or
engine-output bytecode round-trip for these three cases is **not established**.
The existing published for-of differentials and round-trip tests remain regression
evidence only. Do not promote the priority tests to end-to-end completion.

## Validation checkpoint

Full build passes. Fixed source regression65/65; focused tests4/4; requested
sweep236/244. Seven failures match the current user list. The eighth,
async_semantics_test, reproduces bad_variant_access on freshly built wtbase
92e4bfcb and is already recorded in status.md since the earlier async rounds.
Both40-input cohorts are unchanged (591/620 source,537/620 previous), including
all manifest bytes. Complete source hashes are identical. Existing published
for-of Node differentials and SDK comparison tests pass; this does not validate
the still-unpublished nested/sequential/await targets.

New fixture exact entry-derivation limits (bytes are admission boundaries, not
RSS; each limit succeeds and limit-1 refuses):

| Padding | Bytes | Work |
| ---: | ---: | ---: |
| 200 | 1,009,064 | 1,568,563 |
| 201 | 1,010,280 | 1,569,084 |
| 400 | 1,256,360 | 1,673,711 |
| 800 | 1,742,760 | 1,883,803 |

The verifier's exact work limit and limit-1 are checked separately. Full80-job
resource measurement uses four concurrent engines: maximum child40,337,408
bytes plus runner21,643,264 gives a conservative182,992,896-byte bound.
All logs and the generated target table are under build/review/for-of-remaining/.

## Group B continuation: async body / synchronous iterator (2026-10-21)

Baseline `7c4c6912`, HEAD `07c6c665`. **This checkpoint completes 23058 only;
42397 and 46008 remain incomplete.** Group A is not part of this change.

### Original evidence and correspondence

m23306 acquires the synchronous iterator at PC29, reads/caches `next` at35,
calls it at46, checks the result object at51, and branches on `done` at63.
The success edge reads `value` at68. Copies retain this value as the receiver
for the named member call at96 (`specStart`), followed by the original await99.
A rejection reaches the original body handler131, then the guarded close read
149. Normalization maps await99 to working96 and close149 to working125.

The close relation used ordinary raw opcode effects on that normalized body,
while ExceptionFlow used the verified native-await effects. Its PC99/96
correspondence refusal was therefore real, not an iterator-priority gap.
ExceptionFlow now retains the owner-proven AwaitSites with its memory/work
charge. Both completion_relation and close_boundary replay the same semantic
lifter. RawEventGraph accepts a generic semantic owner, without depending on
AwaitRegion or changing ordinary/await-catch query order. The async owner remains
PhasedBody's fresh completion proof; this adapter supplies no new premise.
Unused, malformed or duplicate normalization sites refuse. Rejection bypasses
normal Commit and retains the original Catch dispatch.

The next refusal was the method-call receiver. Successful iteration-value
copies already have independent ForOfKeyDefinition/commit evidence, but the
call checker could not see that native binding identity. A prepaid ValueId
index now records these ordinary binding aliases separately from private
iterator/next/result values. Its lifetime is `kIteration`, never invocation;
phase-corridor fusion cannot extend it across an iteration/handler boundary.
The operand verifier checks the actual target symbol, and recovery reconstructs
the index from the original success-edge commits. No fabricated expression,
source temporary, initialized cell or source-scope assumption is introduced.

The record publishes all six async event methods; for example:

```js
async specStart() {
  let $key2;
  for ($key2 of this.eventMonitors) {
    await $key2["specStart"]();
    continue;
  }
}
```

The independent packaged source, used only for evaluation, says:

```js
async specStart() {
  for (const monitor of this.eventMonitors) {
    await monitor['specStart']();
  }
}
```

All six changed async bodies have the same checked correspondence; the
emitted body starts with `let $key2;`:

| Method | Original body (evaluation only) | Emitted loop |
| --- | --- | --- |
| `SpecEvent.specStart` | `for (const monitor of this.eventMonitors) { await monitor['specStart'](); }` | `for ($key2 of this.eventMonitors) { await $key2["specStart"](); continue; }` |
| `SpecEvent.specDone` | `for (const monitor of this.eventMonitors) { await monitor['specDone'](); }` | `for ($key2 of this.eventMonitors) { await $key2["specDone"](); continue; }` |
| `SuiteEvent.suiteStart` | `for (const monitor of this.eventMonitors) { await monitor['suiteStart'](); }` | `for ($key2 of this.eventMonitors) { await $key2["suiteStart"](); continue; }` |
| `SuiteEvent.suiteDone` | `for (const monitor of this.eventMonitors) { await monitor['suiteDone'](); }` | `for ($key2 of this.eventMonitors) { await $key2["suiteDone"](); continue; }` |
| `TaskEvent.taskStart` | `for (const monitor of this.eventMonitors) { await monitor['taskStart'](); }` | `for ($key2 of this.eventMonitors) { await $key2["taskStart"](); continue; }` |
| `TaskEvent.taskDone` | `for (const monitor of this.eventMonitors) { await monitor['taskDone'](); }` | `for ($key2 of this.eventMonitors) { await $key2["taskDone"](); continue; }` |

The existing assignment-form key proof establishes that this noncaptured
binding needs no source per-iteration lexical environment. The engine input
remains ABC only.

### Remaining cases are not admitted

- **42397:** separate entry/priority certificates pass. The retained production
  boundary still refuses the inner private register27 live-out at753, outer
  body-wrapper ownership, and current-completion dispatch. An experimental
  dead-copy/forest composition reached recovery, exposing an additional route:
  outer return-close failure (705/721, rethrown746) can enter the already-closed
  inner handler760, then the outer handler823. A parent-only target rule is
  incorrect. A future certificate must prove the closed-state bypass, unchanged
  exception payload, absence of another observable close and final destination
  for each original exception edge. The experimental admission is **not** in
  production; scratch evidence is under `build/review/o32-groupb/`.
- **46008:** the second loop's PC717 guard dispatches through H874's environment
  pop and rethrow875 to H880. A pop is not an inert forwarding operation. Its
  environment allocation/parent identity and native per-iteration block exit
  must be matched jointly. Routing evidence alone does not establish a fresh
  captured source binding. The existing `kBodyDispatch` refusal stays.

Neither remaining blocker needs a new language premise. Their scope/completion
proofs are unfinished; the round's three-record completion goal is not met.

### Evidence

The new `for_of_composition` SDK fixture and the extended user-try differential
check ordinary values, exception identity/type, iterator `return` order,
receiver identity and microtasks. Mutation tests cover stale/missing/duplicate
normalization, wrong binding ownership, callee/receiver swaps, missing exception
ports and the existing iteration mutations, with 200/201/400/800 scaling and
exact budget boundaries.

SDK comparison uses `grouping_bytecode.py`'s strict assembly helpers, preserving
every difference. At default optimization both methods and the whole fixture
are byte-identical. O0 preserves acquisition/next/return/object-check counts,
and the recompiled close/evaluation certificate verifies, but raw assembly
is **not identical**: the engine's assignment-form key and retained result use
separate scalar slots, so declaration initialization, register copies, the
explicit continue and resulting branch labels differ. No register/handler
normalization is applied. The default-optimized body is byte-identical, but the
standalone async owner still refuses that optimized shape (`kAwait`); this is
not counted as an independent default re-import proof.


Final checks: full build succeeds; serial source-regression **65/65**; focused
raw/close/evaluation/draft/async tests **13/13**; publication refresh **4/4**.
Requested sweep **227/233**. Its six remaining failures are documented existing
failures: family_source_cli, tree_structure, try_tree, class_tree_source,
try_tree_source and async_semantics. The obsolete async refusal assertions in
for_of_user_try, for_of_multiple and async_failure are replaced by publication
and differentials, not by optional/skipped assertions. Differential cases per
script are **60 / 865 / 756 / 360**, including all six actual event methods.

Against wtbase7c4c6912, both40-input cohorts have zero regressions and no text
changes to previously complete records. Source **601→602/620**; previous flags
**541→541/620**. Full manifest-record comparison changes only o32/23058 under
source, and no records under previous flags. The helper also reports387 skipped
records, which are outside the620 denominator. Node checks the new record and
all newly publishing fixtures. Maximum CLI OS peak RSS across80 runs is
**44,269,568 bytes**; four CLI processes plus their wrappers have a conservative
**246,546,432-byte** bound. Logs, exact SDK diffs, cohort comparisons and the
unpublished experimental routes are under `build/review/o32-groupb/`.
