# Remaining o32 iterator closes: nested entry boundary checkpoint

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
