# Member-update storage regression (2026-10-21)

This checkpoint fixes the priority scale regression. It does **not** complete
ArgumentMatchers/o32/51260 or add a stored-instance-initializer admission.
Baseline: `7003dce3`; no source-language predicate or configured budget changes.

## Cause and representation

The cc bisect identifies `24b01ff0`, after the passing `78278c38`. Reproducing
the failure on a later baseline did not establish an accepted pre-existing
failure. The earlier class-initializer report should not have classified it so.

There were two separate costs:

* `DiscoverMemberUpdates` could allocate three ValueId indexes, a use-count
  index, and reserve OpId-count certificates after merely finding an unmet
  member call. An allocation-free Get/calculation/Set identity census now
  precedes those allocations. The certificate reserve uses the number of
  semantic candidates, not operations. This derives the same original
  references as discovery/verification; it adds no admission rule.
* More importantly for nested_members800, MemberMutationExpr enlarged the
  common TreeExpression arena: ExprNode went from 48 to 56 bytes on arm64.
  This taxes every expression even when no member update exists. Merely
  reducing the discovery arena did not repair this fixture. A member mutation
  now stores either MemberIncrement, or MemberCompound{operator, rhs}; the RHS
  exists exactly for the compound alternative. It fits within CallExpr's
  existing payload width and restores ExprNode to 48 bytes. A unit static
  assertion prevents reintroducing this per-expression regression.

EvaluationPlan additionally retains a nullable immutable shared member-update
plan rather than six empty vector headers. Copies share the immutable plan;
mutation tests explicitly detach their proposed certificate. The existing
RecoveryAllocator charges allocate_shared's actual rebound allocation before
allocation. A successful plan retains that allocation plus actual arena
capacities in allocated_bytes. Independent verification still derives fresh
identity, uses, order, ports and catch evidence; shared ownership is not proof.

No source operator, effect, exception exit or ownership check is removed.
MemberCompound's required RHS replaces an invalid representable state, not a
runtime precondition. Printer and recovery consume the same typed alternative.

## Complexity and exact boundaries

The census adds one linear scan (including constant-arity operand checks).
All aliases use the existing canonical operand index. Existing functional
owner/effect indexes are built only when a real candidate survives; no
per-candidate CFG walk is added. Non-arithmetic stored-value producers are
rejected before visiting their operands: a large-arity call result shared by
many stores must not cause repeated operand scans. A synthetic linear-size
800-argument/800-store graph verifies this with zero scratch bytes and3,206
work units under a bytes=0, work=ops+uses grant. All vector capacities and shared-owner
allocations are prepaid. An unmet reference with no Get/Set match succeeds with
zero scratch bytes; 200/201/400/800 tests force the unmet root so they exercise
this path, rather than the pre-existing successful-reference early return.

Exact-budget tests verify both the independent verifier and construction,
including the shared allocation: the measured byte/work grant succeeds and
one byte/work unit less refuses. The no-candidate path has the same exact work
frontier with a zero-byte grant. No budget has been increased.

Measured depth/width800 costs against freshly built baseline tests:

| Test | Before bytes / work | After bytes / work |
|---|---:|---:|
| member_update independent verification | 9,136,327 / 1,759,389 | 8,080,207 / 1,771,390 |
| guarded_store | 7,789,723 / 3,112,454 | 7,789,723 / 3,112,454 |
| literal_field_forest | 49,498,271 / 4,722,651 | 49,498,271 / 4,722,651 |
| readable_statements | 64,828 / 37,615 | 64,828 / 37,615 |
| native_arguments | 6,409 / 4,005 | 6,409 / 4,005 |

Member-update construction now measures 8,080,391 bytes / 1,772,990 work at800.
The forced-unmet/no-matching-pair case measures zero bytes / 12,013 work.
Native-arguments baseline frontier logging was obtained by linking the same
test-only logging against the baseline libraries; its assertions are unchanged.

For the exact generated nested_members800 ABC, baseline class draft cost is
1,515,130 bytes / 1,780,624 work and enclosure cost is
782,816 bytes / 884,362 work; it aborts in FamilyEnclosure with kResourceLimit.
Afterwards these costs are 1,511,978 / 1,780,624 and
778,368 / 884,362, respectively. All 801 member inner scopes and the class
initialization ports verify. These are individual pass costs, not process RSS.

## Validation

* Full build passes. Source-regression: **65/65**.
* Every CTest matching scale: **11/11**, including class_self_initialization_scale_test.
* Requested five exact-budget tests: **5/5**.
* Broad class/field/initializer/member/update/tree/flow/recovery/logical/CLI/
  receipt/manifest sweep excluding the separately run scales: **208/213**.
  Failures are the documented family_source_cli, tree_structure, try_tree,
  class_tree_source and try_tree_source. No new failure is classified as known.
* Existing real es2abc native-member-update fixtures: **5,852 Node differential
  cases**, **8 observed mutants**; syntax checks run on all published variants.
  Old/new engine outputs pass strict grouping_bytecode.py comparisons for all
  three complete fixture exports at default and O0 (**3 pass / 0 differ / 0
  unverified each**), with three token and Node syntax passes. No allowances or
  register/branch/handler normalization.
* Both40-input cohorts: **605/620 source**, **542/620 previous flags**;
  zero regressions, new completions, complete-source byte changes, or manifest
  record changes against7003dce3.

The40-input source RSS run has maximum CLI peak40,386,560 bytes; the sum of
the four largest CLI peaks is147,292,160 bytes (a conservative bound for the
four exporter subprocesses, not a simultaneous RSS sample).

Scratch: build/review/scale-stored-init, including costs-800.json, before/after
logs, scale-final.log, source-regression-final.log, sweep-final.log, compare-source.log,
compare-previous.log and member-bytecode-verified/report.json.

## Stored instance initializer: still open

Fresh production decoding of m51713 shows slot0 read at PC10 followed by
CALLRUNTIME_CALLINIT at PC12 with receiver v4 (the saved this). The latter
currently returns SemanticError::kUnsupportedInstruction. Creator m51442's
PC40 method occurrence is stored to the private slot at PC45; the current
family reader still refuses its ordinary creation role as
CreationContextError::kKind before reaching that constructor gap.

Pinned interpreter-inl.cpp:1499-1506 gives CALLINIT CALLTHIS0 semantics.
ets_frontend/es2panda/compiler/core/function.cpp:219-233 emits it at base
constructor entry; ir/expressions/callExpression.cpp:123-133 emits it after
successful super for derived constructors. This supports, but does not itself
prove, a once-per-construction native field projection.

The remaining implementation must jointly bind the actual creator environment
and slot to the constructor read, prove private installation before any
construction and no overwrite/escape, recover the genuine method-body role,
then match the initializer's field events and exceptions at each construction
port in OriginalFlow/RecoveryFlow. It cannot be obtained by widening
FunctionKind::kNone, by treating the stored function as an immediate static
initializer, or by suppressing CALLINIT. No new premise has been identified;
this cross-method implementation is **not delivered by this checkpoint**.
The optional Hypium/per-iteration cleanup task was not attempted.
