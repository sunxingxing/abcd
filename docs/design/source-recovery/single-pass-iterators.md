# Single-pass native iterator scopes

A native `for (… of …)` can have no reachable repetition: every body path leaves
through break, return, throw, an enclosing transfer, or divergence. Natural-loop
membership cannot represent that activation. This increment gives those
activations a separate typed scope; it does not alter physical CFG/SSA,
`LoopForest`, or existing `LoopId` identities.

## Owner and activation

`for_of_iteration` owns `ForOfScope`: `NaturalLoopScope{LoopId}` or
`SinglePassIteratorScope`. The shared raw certificate in `for_of_scope` records
acquisition, cached advance, done decision, value Commit/body entry,
exhausted edge/continuation, logical enclosing iterator, original exit cuts,
pending payloads, exception dispatch/protection, coverage and unreachable nominal
repeat entries. Logical parents come from sealed original body membership and
exceptional dominators, for natural and single-pass alternatives alike.

The legacy route always runs first. The retry requires its structured
missing-natural-loop (`kAdvance`) or borrowed-header (`kEntry`) witness; the proof
replays that failing legacy consumer and matches protocol/acquisition/advance and
borrowed identity. A bare error enum never activates it. The original
multi-exit/placement causes stay in the failure chain. A headerless summary only
selects a legacy query; it never grants admission.

## Six obligations

1. The existing identity/reference certificate proves original acquisition,
   cached `next`, receiver identity, dominance and single evaluation in order.
2. Each activation advances once. The original done decision's empty
   edge reaches exactly its retained continuation without closing the iterator.
3. The nonempty edge reaches the original value Commit and source binding once;
   existing binding/capture/operand evidence owns storage identity and Commit.
4. Sparse edge-qualified state propagation covers every reachable path until a
   sealed completion cut, terminal action or divergence. Re-entering advance or
   acquisition is refused. Unreachable original nominal-repeat entries remain
   explicit evidence; a dead compiler jump is never a fabricated back-edge.
5. Native step exceptions retain original dispatch; original close cuts retain
   their pending completion. Existing completion ports prove close order,
   priority, internal-failure selection and zero additional close events.
6. Acquisition remains at its original execution point inside the independently
   verified enclosing logical scope. Repeated outer activations therefore
   execute acquisition again; neither the inner nor outer scope borrows a
   physical header or caches acquisition across activations. This is freshness
   of the iterator record, not an assumption that user factories return distinct
   objects.

Pre-binding exceptional exits are admitted only when owned by native
advance/result-check/done/value events. All other exceptional behavior keeps its
refusal. No resource-failure premise or iterator-acquisition premise is added.

## Placement and independent checks

Control planning inserts `IteratorScopeControl` at advance after acquisition,
retains the exact exhaustion continuation, and cuts only the proven lexical body
continuations. It keeps physical arcs and loop identities. The emitter owns one
native `ForOfStmt` at acquisition and maps the initiating native break to that
logical scope. There are no emitted `return()` calls. Array-pattern acquisition
and close operations keep their own certificate; ordinary user tries keep their
existing owner and protection context.

`VerifyExceptionControl` rebuilds ordinary control and certified lexical
placement and compares every node/edge/loop/unit/region. Iteration/evaluation and
emission-view checks reconstruct identity, count and placement. Operands and
origins independently rebuild the raw certificate. Tree flow checks the exact
native scope/body/break target and edge origin. OriginalFlow retains a separately
rebuilt immutable certificate. RecoveryFlow compares it and traverses actual
native flow to reject reachable implicit repetition or reacquisition; existing
close/recovery checks prove payload, priority and logical enclosing close routes.
The earlier exhaustion-continuation certificate carries this same scope view
when both lexical repairs are needed. All downstream owners consume one view.

The scope retry grants no later expression/Reference permission. A method that
progresses past an old partial input/control omission can still refuse at an
independent expression owner. The existing omission policy is not broadened by
this increment; such a next blocker stays typed and must be reported explicitly.

Iterator-only OriginalFlow evidence is cold, with an explicitly prepaid header
allocation only for methods that have iterator evidence. Its value-copy wrapper
preserves mutation isolation. This avoids charging a larger optional header to
every ordinary function, and keeps the existing nested-800 class scale test
within the unchanged 64 MiB stage budget. No verifier or fact is removed.

## Resources and refusals

Shared indexes, exceptional dominators, sorted coverage incidences and sparse
worklists target O((N+E+U+K) log(N+K+1)) work. Admitted sparse state/incidence
products have prepaid linear caps; pathological dense products return typed
resource failures. Budget grants include retained legacy inputs and retry peaks;
output capacity and PMR scratch are charged before allocation. Failed delegated
legacy queries without work telemetry precharge bounded exclusive grants.

`SinglePassError` distinguishes input, activation, entry, repeat, exit, parent,
protection, payload, coverage, correspondence and resource/work limits. Original
placement/control causes remain. Nested resource/work causes keep their limit
classification and the original region/two-exit witness through route failure
projection; a focused envelope test checks both alternatives. Neither unsupported `for await` nor unrelated
multi-exit user tries can activate this route. Nested unconditional returns that
already refuse at the lower close-entry continuation owner remain refused before
placement; admitting those requires a separate close-entry design.

## Evidence and remaining boundary

The pinned O0 `single_pass` fixture covers synchronous/async terminal bodies,
ordinary try plus array patterns, logical parents without natural loops, and
suspension around and inside activations. The Node oracle checks values,
identity, lookup/call/close order, primitive and thrown close failures and pending
throw priority. Owned unit mutations check all certificate streams and actual
native-repeat reachability; register renumbering, harmless copies and block
splitting are invariance positives. The 100/200/201/400/800 test measures fixed-K
scaling, not unbounded dense products.

Service m60551 now passes the scope proof for original acquisitions 452/1307.
Its next separate blocker is a member Reference at original 2208 / working1512,
read at2132 with a selected/coerced argument in the user catch. That argument
ownership mechanism is not extended here. The exact acceptance receipts and
cohort/CTest comparison are written under `build/single-pass/`.

Review fix: an earlier safely omittable phased input/control refusal survives
successful scope discovery. If later family tree construction has a member-local refusal, the class
publishes that same omission and separately reports the later cause. Preparation
reserves every family owner ID before construction; a later omission retains its
reserved range. The finished-class owner verifier checks those ranges for bounds,
disjointness and complete occupancy independently of actual emitted trees. No
omitted range supplies an AST or source receipt. Resource/work failures stay
terminal; immediate and split preparation retain distinct cost ownership. This
preserves stable partial text without widening the accepted body semantics or omitting arbitrary failures.

The comparison now checks all record states, partial omission sets and all
published text. Source-regression65/65 and related42/42 pass. All40 preserve619
complete plus one partial record byte for byte. New receipts are under
`build/single-pass/partial-fix/`. Final full CTest is949 pass /34 known failures
out of983 in3,724.10s, with zero new failures and no missing/skipped tests.
The final ISA ledger stays222/21/61/28.
