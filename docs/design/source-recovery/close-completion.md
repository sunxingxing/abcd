# Close completion diagnostic analysis (A3b, partial)

`close_completion` is standalone and has no production hook. It re-derives
`close_entries` and the original completion relation, and records original
members and edges (including exception cuts). It does not grant topology
ownership or source admission. No original region, instruction or edge is
mutated; A3d discharges impossible query paths only with comparison witnesses.

The proof states are intentionally separate:

- Guard and return-reference facts reuse the independently checked A3a activation
  proof, plus the unique callee use and receiver identity from the immutable IR.
- For a Throw entry, priority requires every reported exit to retain the entire
  original Throw tuple. An exit-free cycle does not prove this obligation.
- A3c adds outcome-indexed non-Throw priority. Success/object and nullish
  outcomes must keep the entry completion; getter/call failure must propagate
  that exact close exception; an object-check failure must propagate that
  check's exception identity. Any unproved outcome or conservative sentinel
  path keeps the entry's priority obligation missing. See the A3c section below.
- Ownership records raw membership and checks external predecessor edges and
  semantic roles. It remains missing: private scalar live-outs and overlapping
  user-try exclusion have not been proved. An arbitrary object check does not
  count as a return-result check; its operand must be this call's result.
- Nested current-completion propagation has not been implemented. Multiple
  acquisitions are conservatively missing for this obligation, including
  unrelated acquisitions; this is a diagnostic coverage gap, not a restriction
  on source semantics or permission for a production path to refuse a program.

The verifier re-derives the immutable entry proof with A3a's backwards guard
algorithm and recomputes the summary; it does not consume supplied member masks
or verified flags. Priority/reference transfer definitions are shared; this is
not an independent implementation of the pinned interpreter. Telemetry is not a
certificate field. `peak_bytes` is a conservative scratch bound including
exclusive delegated grants, not measured allocations or RSS.

Every PMR allocation and surviving vector capacity is prepaid. All sparse walks
and sorting are charged to one input-sized N log N admission grant; dense
repeated-region inputs refuse work rather than admitting quadratic work.

The unit test covers the existing real m423/m451 ABC, original-payload Throw
preservation, conservative Break/Return refusal, forged flags/payload/targets,
missing/duplicated members, missing/changed edges, wrong receiver/reset/guard
inputs through A3a, register permutation and edge splitting. 200/201/400/800
prefix-size cases test exact producer byte/work admission and verifier work
boundaries. These are not dense completion/nesting scaling tests. No new Node
or runtime priority-matrix equivalence is claimed.

Remaining work: complete private ownership and user-try exclusion, nested
current-completion bridges and their fixture/mutation matrix. A3c's outcome
relation closes the absent-outcome representation gap, but does not remove
conservative raw exception/sentinel paths or grant topology ownership.


## A3c: outcome-indexed completion

`AnalyzeCloseOutcomeRelation` is a separate synchronous analysis query; the A2
API and its value/state specialization are unchanged. Both use the raw event
engine. The new specialization carries the outcome in the same interned state
as the entry, persistent SSA environment, pending payload and control target.
No independent sets of kinds, payloads or outcomes are joined.

The source analysis derives the observed return-read and call from the checked
A3a occurrence/reference evidence. The raw observer identifies getter/call
normal Commit and original exception edges. Copies and edge-qualified phis
preserve typed return-method/result provenance. Loose comparison of the method
value with undefined/null, followed by typed Boolean predicates, supplies the
nullish edge. A normal object check supplies SuccessObject only when its operand
is this call's result on that path; its exception supplies PrimitiveResult.
No adjacency, register number, same-block or exact region shape is required.

GetterThrow and CallThrow carry the actual observed operation's exception-exit
identity. Catch entry and subsequent stores/phis propagate that identity through
the existing relation; the priority table never substitutes the entry payload.
The builder observes normal Commit departure. Verification replays the immutable
original edges with the opposite worklist order and derives success on entry to
Commit, separately from the builder's event-stage transitions. Pinned scalar and
Boolean meanings are shared. Supplied outcomes and classification are not proof
inputs. The source verifier also checks every saved outcome tuple.

At A3c every raw may-exit remained (A3d below refines proven comparisons).
Unknown non-protocol exceptions are Unproved with
OtherException and the real source node. An unresolved Hole comparison retains
its two successors, the observed outcome, and a SentinelPath uncertainty witness;
it is never silently resolved. For example, m423 can conservatively reach Break
after GetterThrow through this comparison: this is a missing proof, not a verified
swallowed close error. No result is returned on state/work/byte exhaustion.

The existing real es2abc fixture is sufficient: its iterator is an opaque input,
so its expanded close code contains the getter failure, call failure, nullish,
object-result and primitive-result alternatives. m423's Break and Return entries
both expose all five outcomes. Their first missing witnesses remain the raw
nullish-comparison exception edges (PC114 and PC205); their sentinel may-paths are
also explicitly recorded. No compiler-generated edge is removed to obtain a pass.

The unit test verifies that erasing outcomes yields exactly A2's original may
relation. It checks outcome swaps, original/close-error replacement, primitive
misclassification, removal of uncertainty, payload/target identity, and the
priority transfer table including outer Continue and original Throw. Pure table
tests do not mint raw certificates. Register permutation and split-edge variants
are included. 200/201/400/800 NOPs are inserted *inside the traversed close path*,
with exact producer byte/work and verifier work boundaries and state exhaustion.
All new PMR nodes, state/output capacities and retained results are prepaid.
The existing linear sparse-state cap and input-sized N log N admitted-work cap
apply; there is no resource-limit increase or production hook.


## A3d: domain-qualified comparison refinement

This remains a standalone outcome analysis, not production topology ownership.
`NonThrowingComparison` records entry, original Before node, typed operator,
operand domains/identities and optional equality. The verifier re-derives these
witnesses; a supplied fact never suppresses an edge. Facts and their retained
output capacity are prepaid. Only the outcome specialization consumes them;
A2's original may relation and raw exception CFG remain unchanged.

PC114/205 of m423 are **EQ_IMM8_V8**, with a proven undefined accumulator
(loaded at PC113/204) and the return-method value in v16/v17. The pinned runtime
is OH7 `f04900cf951c66c2ea18b2bab5b591d5336c34b9`:

- [EQ/NOTEQ dispatch](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L1934)
  uses FastEqual before any slow call. The strict dispatches are at 2375/2394.
- [FastEqual](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/fast_runtime_stub-inl.h#L90)
  returns directly when either operand is undefined/null; no ToPrimitive, call
  or language exception occurs. FastStrictEqual at 123 likewise returns without
  string/BigInt comparison when one operand is nullish.
- [Slow equality cases](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/js_tagged_value.cpp#L462)
  at 493-640 show the primitive conversions and the object ToPrimitive gate;
  null/undefined comparisons do not enter that gate. Concrete constant pairs
  (the tracked Boolean/int/null/undefined/Hole domains) cannot execute user code.

The fact is derived from the actual SSA operands, not opcode adjacency. Equality,
inequality and their strict variants share the pinned typed semantics. An
unknown operand paired with a non-null constant does not qualify; its exception
edge remains. Bytecode branches that are already non-throwing need no such fact.

The Hole test is **not** the closed Boolean flag. It is the saved-close-error
sentinel: m423 PC100 puts Hole in v15; Catch128 overwrites v15 with the close
exception. PCs130/132 copy it to v16, PCs134/135 put Hole in v17, and PC139 is
NOTEQ_IMM8_V8 followed by the decision at 142. The Return copy uses the analogous
NOTEQ at 230 and decision at 233. The actual closed flag remains v7.

On the normal close path, SSA/copy/phi flow proves Hole versus Hole.
FastEqual's tagged-identity test returns true, so NOTEQ is false: both that
comparison's exception edge and the spurious explicit-Hole rethrow are excluded.
There is no register/PC/shape admission rule and no new language premise.

The Catch path is still **unknown**, not automatically non-Hole. This raw API
has no qualified exception-domain certificate. In particular,
[JSThread::SetException](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/js_thread.cpp#L335)
accepts the tagged value without excluding Hole; interpreter THROW at 4049 and
Catch dispatch at 3999/4024 do not establish a non-Hole payload proof. We do not
import an unstated "all caught values are language" premise. Thus the first
missing non-Throw witnesses move from PC114/205 to **PC139/230**
(`kOutcomeUnproved > kOtherException`), and unknown-sentinel branches retain
`kSentinelPath` at their real decisions. Break and Return remain missing.

Tests re-use the pinned ABC. They assert the nullish facts and pruned exception
exits, concrete Hole==Hole evidence, unknown-sentinel preservation, all four
equality operators, null vs a non-null Boolean mutation, and a sentinel read
aliased to an unknown register. Forged/removed comparison facts are rejected by
both verification APIs. 200/201/400/800 traversed-path and exact resource tests
remain. The refined projection must be a subset of A2's unchanged may exits.


## A3f: contract-qualified Catch payload domains

The 2026-10-16 approved `ExternalExceptionNoHole()` premise is independent of
all prior premises and is enabled only in LanguageSourceV4. The standalone
close API receives the contract explicitly; its default remains the pinned
contract. V1–V3 do not acquire the premise.

`DeriveCatchPayloadDomains` builds one conjunction per original Catch entry
from **every** dispatch source. Local explicit throws depend on the thrown SSA
value (constants, copies and phi inputs); they never use the external premise.
The common user-code effect model identifies external exception boundaries.
Known error-producing checks have a separate runtime-error fact. The least
fixed point cannot justify an unsupported/cyclic definition, and one possible
Hole source prevents the Catch conclusion. A normal jump into a handler's
instruction stream never supplies its Catch payload.

The outcome relation records these facts and independently recomputes them in
verification. Only a qualified non-Hole value compared with Hole obtains the
`kCatchNonHole` comparison witness, excludes the comparison's impossible error
edge and resolves the sentinel branch. No producer/source expression is forged;
original exception identities still determine the close error payload.

On the real m423 fixture, both Break and Return priority entries verify under
V4. V1–V3 still report `kOutcomeUnproved > kOtherException` at PC139/230.
Ownership and nested-close proofs are unchanged; this is not publication or a
grant of ProtocolOwned topology. Tests cover local Hole, mixed sources, copies,
real SSA phis, conservative cyclic equations, nested catch/rethrow, mutations,
and prepaid 200/201/400/800/exact-budget boundaries.


## A3g partial: all-state event coverage, ownership remains missing

This standalone increment does **not** complete A3g or grant protocol ownership.
The outcome relation now records a conjunction over every visited state at each
original Before node: scalar, operand-domain-proved comparison, and this call's
result-object check. A single unproved state keeps the event unproved, regardless
of worklist order. The sparse map uses the existing prepaid arena; the sorted
retained vector is charged before allocation. Verification rebuilds it with the
opposite worklist order. Member refinement consumes only this checked ledger;
an absent event is infeasible relative to the complete derived-entry set, not a
claim that an arbitrary instruction is dead.

The fixture m423 has no unclassified member after refinement, but ownership
still reports `kOwnership / kPrivateState`: private SSA live-outs and the full
region/user-try boundary proof are not implemented. This label identifies a
missing analysis, not an observed escaping value. No production path consumes
these facts. The standalone probe uses an unknown frame receiver, conditional
capture/local/module premises, and V4 arithmetic; it does not invent source
admission for the method.

Standalone V4 results on the requested original methods:

| Method | Priority | First missing obligation |
|---|---|---|
| o5 m28799 | verified for derived entries | ownership: external entry at PC1182 |
| o20 m7171 | verified | ownership: external entry at PC215 |
| o32 m39073 | verified | ownership: external entry at PC300 |
| o32 m47128 | verified for both acquisitions | ownership: external entry at PC434 / PC934; multi-acquisition propagation unproved |
| o32 m42717 | not reached | entry derivation `kContinuation`, PC692 |
| o32 m40502 | verified | ownership: external entry at PC445 |
| o32 m23306 | not reached | entry derivation `kExternalEntry`, original PC99 / normalized PC96 |

The external-entry witnesses are real predecessor edges into the current member
cut; they cannot be waived merely because the destination is completion code.
The missing full boundary/entry proof must decide which edges are legal ports.
Multiple acquisitions alone do not establish nesting: the existing conservative
multi-acquisition gate is not a nesting proof.

The nested fixture m395 stops at `kContinuation` PC226, just as m42717 stops at
its outer guard PC692. A3a's continuation derivation cannot yet relate that guard
to the current inner-close completion. A2's exported completion tuple also does
not carry a boundary SSA-state certificate. Completing A3g therefore requires a
parent-linked continuation/activation proof and a correlated inner-exit to
outer-entry state bridge, followed by private-state live-out and scope ownership
verification. Skipping the guard, comparing payload IDs alone, or granting
ownership from an event-membership table would be unsound.

Tests added here cover event-ledger deletion/node/proof mutations, existing
200/201/400/800 and exact-budget runs, a production-independent diagnostic probe,
and an explicit nested-derivation-gap regression. They are prerequisite tests,
not the requested complete ownership/nested mutation matrix.


## A3h: original boundary, ports and private-slot liveness

The six `kExternalEntry` witnesses from A3g are not live entrances. Each is the
normal Commit edge of a result-object check with no path from the method entry
in the original Before/Commit/Catch graph (including the pinned exception
dispatch). Its destination reloads/rethrows the original pending exception:

| Method | Dead Commit edge | Loop-exhaustion target (distinct) |
|---|---|---|
| m28799 | 1179 -> 1182 | 1189 |
| m7171 | 212 -> 215 | 221 |
| m39073 | 297 -> 300 | 307 |
| m47128 first / second | 431 -> 434 / 931 -> 934 | 441 / 941 |
| m40502 | 442 -> 445 | 452 |

These are syntactic continuations of unreachable result-check paths, not
exhaustion edges, reachable user edges, or new legal entry ports. The boundary
proof independently rebuilds `RawEventGraph`, matches every original flow arc
and caller exit, and records the unreachable predecessor edges. Every live
entrance must still be a derived close entry. Every outgoing member edge must
be an original completion-relation cut. Entry-qualified ports retain the exact
exit, correlated pending completion and close outcome; they are never inferred
from source layout or a join PC.

Private-slot liveness includes **acc as well as registers**, conservatively
collecting every slot written by close members. Every normal/exception port
starts a sparse raw-event query: read-before-write refuses; only a normal
Commit (or an actual Catch acc definition) kills; exception edges bypass the
normal write. Normal jumps into handler code never get the Catch kill. Cycles
without reads are valid deadness evidence only, not proof of progress or
permission to remove control. Cell-backed private state is not certified by
this register analysis; a lexical store keeps `kPrivateState` missing.

Candidate try association remains only a necessary condition. Return-protection
roles must match the real return-read dispatch; enclosing-handler roles must
match the derived Throw entry's actual Catch. An intersecting unassociated or
role-mismatched try refuses `kUserTry`, including a forged candidate association.
A region is listed as covered only when **every originally reachable Before in
both its protected range and its handler belongs to the close member set**.
Otherwise `kRegionCoverage` records the real TryId and first uncovered node.
The producer's covered-region list never substitutes for this check.

This exposes a remaining ownership gap instead of granting ownership from the
candidate scan: the outer body-protection ranges include ordinary loop-body
operations outside the current close member set. A3h does not relabel these
operations as close code or absorb their effects. Closing this gap requires
separate coverage of the ordinary body and the close subregion, with their real
scope/dispatch correspondence. This increment is standalone and grants no
ProtocolOwned topology, source publication or nested transfer proof.

| Method | Entries / ports | Boundary entrances, cuts, private slots | Remaining ownership |
|---|---|---|---|
| o5 m28799 | 43 / 172 | verified | `kRegionCoverage`, TryId2, PC720 |
| o20 m7171 | 2 / 8 | verified | `kRegionCoverage`, TryId2, PC135 |
| o32 m39073 | 6 / 24 | verified | `kRegionCoverage`, TryId2, PC146 |
| o32 m47128 first | 11 / 44 | verified | `kRegionCoverage`, TryId4, PC218 |
| o32 m47128 second | 2 / 8 | verified | `kRegionCoverage`, TryId6, PC699 |
| o32 m40502 | 7 / 28 | verified | `kRegionCoverage`, TryId2, PC276 |

All have seven conservatively private slots including acc. Priority remains
verified under V4; m47128's separate multi-acquisition/nested obligation remains
missing. The m42717 continuation and m23306 async entry gaps are unchanged.
The m423 Break/Return copies additionally retain a real private-state live-out
at PC253 when a close failure enters another close copy; this is not waived.

The independently rebuilt summary compares entry/port/private/region proof
states, exact ports, dead edges, slots and covered TryIds. It reverses raw
successor traversal and chooses missing witnesses canonically. All PMR buffers
use a prepaid arena; returned vector capacities are prepaid and retained in the
owning proof's ledger. Sparse states and overlapping range walks consume the
shared input-sized O(N log N) work allowance; dense work exhausts with a typed
resource/work refusal, never a successful partial certificate.

Tests cover a real external entrance, a legal port, omitted cuts, live-out guard
and acc reads, overwrite-before-read, a real unrelated try, forged candidate/role
association, certificate mutations, opposite traversal, and 200/201/400/800 with
exact work/byte boundaries. The real ABC probe retains original IDs and PCs.


## A3i: split loop-body wrapper and close-code ownership

`ProtectedRangeRole` distinguishes `kCloseCode` (inner return protection) from
`kLoopBody` (the outer body wrapper). The latter owns only the original dispatch
edges and handler/close subgraph. Its `ordinary` nodes retain their original
Before/Commit identities and normal links; they are disjoint from close members.
The covered-TryId list is NOT an instruction-ownership mask.

CloseEntries discovers the Value-property read by its resolved IteratorKey and
canonical SSA base: the result of this acquisition's unique advance. Missing or
ambiguous value reads do not invent a body start. The independent entry verifier
recomputes this identity. From the successful value Commit, the body proof walks
original normal edges until the already-derived backedge/header or an abrupt
exit. Entry dominance is computed once per method (including original exception
edges), borrowed without another storage charge by each sparse body query, and
rebuilt during verification. No protected PC range is used to discover the body.

The protected range is then checked against this control region. Key/binding
register transports at entry and inert connectors to the backedge can straddle
encoded try boundaries: these are recorded separately as ordinary `transport`
nodes. A transport must have one normal successor, no language effects and no
possible throw, and belong to the proved entry prefix or exit suffix. No copy,
register write, control node or source operation is moved or eliminated. All
other ordinary body operations must be protected; every reachable protected
operation must be in the derived body. A range reaching an exhaustion target,
handler, next header or unrelated operation refuses `kBodyRange`.

Every protected throwing operation must dispatch to this wrapper's actual Catch
and match a previously derived Throw entry with its real source edge. Its
exception payload is not guessed. `kBodyDispatch` identifies a different handler
or missing source entry. A normal return or exit bypassing the close refuses
`kBodyExit`; same-loop continue keeps its original backedge. Handler coverage and
private-state/priority/reference proofs remain mandatory. This is not a generic
relaxation of try control, and the production topology is unchanged.

Real V4 standalone results (counts are original event nodes, including phases):

| Method / close | Wrapper | Ordinary / dispatch / backedges | Close ownership |
|---|---|---|---|
| o5 m28799 / read1148 | TryId2 | 320 / 43 / 1 | verified |
| o20 m7171 / read181 | TryId2 | 28 / 2 / 1 | verified |
| o32 m39073 / read266 | TryId2 | 84 / 6 / 1 | verified |
| o32 m47128 / read400 | TryId4 | 140 / 11 / 1 | verified; separate multi-acquisition/nested obligation stays missing |
| o32 m47128 / read900 | TryId6 | partial: 148 / 1 / 1 | `kBodyDispatch` PC717 -> TryId1/H874, not TryId6/H880 |
| o32 m40502 / read411 | TryId2 | 108 / 7 / 2 | verified |

The second configService loop has a per-iteration environment. Its inner
TryId1 handler at PC874 restores that environment and rethrows at PC875 into
the outer close handler. This effectful forwarding is not erased or treated as
a direct wrapper edge. The m42717 nested and m23306 async entry derivation gaps
are unchanged. The m423 fixture's Throw-copy body also retains `kBodyDispatch`
PC105, where an inline Break close dispatches to its own return protection;
its Break/Return copies still retain their PC253 private-state live-out. None
of these gaps is a new source-language premise.

The independent summary verifier re-derives the split and compares the role,
ordinary/transport nodes, source dispatches and exits, in addition to the
existing entry, priority, member, private-state and exception evidence. The body
walk uses sparse prepaid sets and sorted predecessor pairs; it never builds a
dense per-loop matrix. The original node/commit and dominance indexes are shared
at method scope. All delegated temporary peaks are observed before retaining
only result capacities; exhaustion remains a typed whole-query failure.

Tests add a protected body positive; extending the range past the body; routing
through another handler; a bypass exit; a wrong value identity; a missing Throw
entry; borrowed/fresh index and reversed traversal equivalence; split-certificate
mutations; and 200/201/400/800/exact-budget cases. Existing register/copy and
split-done ABC variants also run the new proof. The real m451 fixture now verifies
its wrapper and ownership. The m423 probe expectation changes from the old
uncovered-body location to its actual inline-close dispatch gap; refusal remains.
No source publication or production hook is part of A3i.
