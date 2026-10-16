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
