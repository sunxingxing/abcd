# Edge-qualified explicit-throw payloads

Checkpoint: 2026-10-20, baseline `2186fbff`. The source oracles were inspected
only for evaluation; all production facts below come from the ABC.

## Diagnosis

Both `query` methods contain a synchronous for-of, break-close, body-close
rethrow and an outer user catch. This is neither an await-input problem nor a
missing captured/local binding. The catch parameter already has source identity.

| Record/method | Saved close-error state | Non-Hole throw | Observing user catch |
|---|---|---|---|
| o37/20175 m20512 | PC637 `ldhole`; PC638 store v17; H665 may replace v17 | PC676 `noteq` compares the saved v18 with Hole in v19; false goes to PC685, true loads v18 at PC681 and throws at PC683 | H759 -> v4 -> v10 -> PC800 `error.code`; PC804 adds it to the message |
| o38/25106 m25655 | PC679 `ldhole`; PC680 store v16; H707 may replace v16 | PC718 comparison; PC723 load and PC725 throw | H801 -> v4 -> v10 -> PC842 `error.code`; PC846 message addition |

Original exception SSA in m20512: V236=Hole at PC637; V237 copies it to v17.
Catch V255 (H665) supplies a Language exception and V256 copies it to v17.
Phi V33 joins V237/V256. V247/V248 copy that identity through acc/v18; V254
is the PC681 copy used by THROW. The old solver forwards all of V254's
Language|Hole domain into H696 (V257), then into H759 (V296). V297/V309/V310/
V311 copy the catch parameter to the property receiver. V312, the PC800
property result, becomes Unknown because the receiver incorrectly includes
Hole; the PC804 add observes V312 and fails input verification. m25655 has
the same equations with phi V34, explicit payload V265, H738 V268 and H801
V310; property result V326 is observed at PC846.

The Hole alternative is infeasible on the explicit throw edge. No external
exception invariant is added or strengthened here: even a local raw Hole
remains possible everywhere except a path proven unequal to that same Hole.

## Proof and integration

`DeriveExceptionThrowDomains` builds one shared index over the original
ExceptionFlow/ExceptionSsa. Scalar copies are canonicalized, but separate phis,
heap/cell reads and captures are never equated. A semantic equality/inequality
with a proven LoadHole result supplies a candidate non-Hole edge constraint.
The equality Boolean's copies must feed the branch. Splitting every original
edge gives genuine edge dominance, including exceptions and normal entries to
handler code. A constraint applies only to an explicit throw of the same SSA
identity dominated by that unequal edge. A shared iterative SCC computation
conservatively declines a definition in the branch's cycle, where an earlier
activation could otherwise supply a stale constraint. This is a documented
coverage limit, not a same-block or compiler-layout rule.

Pinned evidence: `references/openharmony/arkcompiler/ets_runtime/ecmascript/`
`interpreter/interpreter-inl.cpp:1934-1977` implements EQ/NOTEQ by FastEqual,
inverting the Boolean for NOTEQ, with slow-path abrupt completion retained.
`interpreter/fast_runtime_stub-inl.h:90-97` returns true for identical tagged
values, including Hole; strict equality has the same identical-tag fact at
`:136-138`. Thus a normally completed unequal result cannot have compared
Hole with itself. This argument does not claim arbitrary abstract comparisons
are effect-free or nonthrowing; their original exception edges remain.

`VerifyExceptionThrowDomains` independently rebuilds all indexes and expected
constraints from immutable inputs. Value propagation filters only the caught
contribution of the certified explicit throw. It never globally upgrades the
source SSA value, discards an external exception source, or removes a guard.
Other internal categories survive the filter and continue to refuse. The
existing pending-exception/call/module/capture contracts are unchanged.

PhasedBody invokes this only after ordinary input verification reports an
internal value. Every formerly successful plan uses precisely its old route.
The resulting source catch binding retains its original reads/effects and
exception dispatch. Existing source/evaluation/tree/recovery checks still run.

If this progresses an *existing* omitted-member input gap to a located
`kMultipleExits`, retain that omission's provenance and complete consumed-budget
accounting. No unrelated control refusal becomes an omission, no constructor
can be omitted, no body/signature is fabricated, and complete-only mode still
refuses. JSON records the new first cause and region/exit evidence on both CLI
and manifest paths; original input provenance remains internal to eligibility.

## Complexity and resources

One edge-split full-CFG LT dominator computation, one iterative SCC computation,
one copy-root index and one sorted guard-interval index: O((N+E+V+U) log(N+V))
time and O(N+E+V+U) storage. Nested guard intervals for the same identity are
coalesced before per-throw binary lookup. There is no per-value CFG walk.
PMR scratch is charged before allocation, delegated dominator peak is observed,
and all retained proof buffers/work are charged. Exhaustion never proves a
payload Language. The verifier rebuilds once, not per handler.

## Acceptance boundary

Both RpcTool query inputs now verify, under both flag sets. They remain omitted
members: m20512 reaches `kMultipleExits` at TryId0 PC640/H665 (normal exits
PC654 -> PC690 and PC663 -> PC667); m25655 reaches TryId0 PC682/H707
(PC696 -> PC732 and PC705 -> PC709). These are the effectful iterator-return
protection regions; this round grants no ProtocolOwned topology permission.
Neither record/query is published and o36/11209 is not attempted.

Unit fixtures cover branch-selected payloads, copies, inverse equality,
external entries, changed payload/edge/branch, omitted/duplicate evidence,
local raw Hole throws, retained other internal categories and stale cyclic
activations. Width and depth 200/201/400/800 exercise exact byte/work budgets.
At width800: 1,756,691 work and 4,758,692 bytes for discovery.

Pinned es2abc fixtures cover nested user catch/rethrow and the actual close
sentinel, a class-member version (partial policy) and a standalone close.
O0 handler output publishes under both contracts; Node compares object/
primitive/null errors, throwing property access and traces. Close fixtures
assert the exact next refusal, never count refusal as semantic support.
Default-optimized inputs keep their independent CatchRegionError refusal.
The SDK report uses grouping_bytecode.py's strict assembly reader, identical
options and an identical AST string-export adapter on both inputs. All four
compared methods and both full assemblies are byte-identical (default/O0).
No register/branch/handler normalization or allowed-difference rule is used.

Final validation: full build; serial source-regression 65/65; focused tests
4/4; requested sweep 178/192 with exactly the supplied fourteen pre-existing
failures. Both four-worker cohort runs retain the complete sets and every
complete source byte (537/620 previous, 589/620 source). Only the two existing
omissions change cause/location; their surrounding recovered text is unchanged.
CLI/manifest parity and target partial syntax checks are 4/4. Scratch evidence
is under `build/review/rpctool/`, including complete method/assembly SDK diffs.
