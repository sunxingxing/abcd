# Frame Argument Snapshots

GETUNMAPPEDARGS belongs to group 1. This document specifies its remaining common
pipeline work; it is not a source-completion receipt. Reference identities are
the pinned v7 lock, not the host JavaScript engine.

## Runtime Evidence

- interpreter-inl.cpp:1765 calls GetNumArgs with rest index zero, invokes
  GetUnmapedArgs and commits acc only after abrupt-completion checking.
- GetNumArgs at :8007 requires HaveExtraWithCallField. The first physical argument
  is numVregs plus the present function/new.target/this prefix slots. Actual count
  can differ from declared count and comes from the physical call-frame trailer.
- runtime_stubs-inl.h:2276 allocates a new backing array on every invocation and
  copies sp[startIdx + i]. These are the slots at the instruction's execution
  point, not immutable original parameter values or a cached arguments object.
- RuntimeGetUnmapedJSArgumentObj at :2957 creates a fresh JSArguments object with
  length, indexed elements, the retained ArrayProtoValuesFunction iterator and
  non-enumerable, non-configurable caller/callee throwing accessors. Intrinsic
  identities must not be inferred from mutable global or prototype properties.

A local Node strict-function probe confirmed that repeated native `arguments`
reads reuse one object, assigning a formal does not update its element, and it
has no own caller property. This is counterexample evidence against a direct
translation, not execution of the Ark bytecode. A rest array also has the wrong
identity, prototype, brand and descriptors.

## Immediate Proof Boundary

ProveRestPrologue promises that later instructions do not read the original ABI
rest slot. GetRegisterAccesses sees only explicit register operands and therefore
misses GETUNMAPPEDARGS. The proof now rejects a later snapshot, including an
operand-free encoding: it may read the original rest slot when that argument was
supplied. Existing single-allocation rest recovery remains unchanged otherwise.
The regression first demonstrated incorrect admission, then passed after the
guard. This closes a prerequisite; it does not implement argument snapshots.

## Implementation Sequence

The compact `ArgumentWindow` now validates canonical hidden-slot order, runtime
count widths and declared bounds using `BuildFrameLayout`. Its typed
`ActualArgumentCount` partition separates the supplied current-slot prefix from
the extra tail without expanding either. `ProveRestPrologue` consumes this shared
validation; a displaced parameter prefix previously passed its pattern proof.
This component does not yet certify a callable owner or produce count/tail
bindings in semantic IR. It does not convert declared count into actual count.

`GETUNMAPPEDARGS` now lifts to `ArgumentSnapshot` when the shared lift receives
the actual `FrameLayout`. The ordinary function input supplies that context.
Its normal effects contain the declared-slot `RegisterRange`, accumulator write,
ArgumentsRead and Allocation; exceptional effects retain may-throw without an
accumulator commit. SSA consumes these effects before renaming. The semantic
range has a 32-bit count independent of the still-16-bit encoded CallRange.
Missing frame context remains unsupported; malformed layouts are rejected.

Input verification now requires an explicit `ArgumentFrameFact`: the actual
count and extra tail are inputs of the current callable invocation, under the
existing legal-JS entry-value contract. FunctionInput supplies this only after
matching its resolved creation context's file identity, target and frame, and
retains the exact creation occurrence in its immutable owner stamp. A raw layout
or opcode does not acquire that premise. Arrows still refer to their own VM
invocation, not the enclosing source `arguments` binding.

`VerifyArgumentInputs` independently checks original opcode/PC, canonical frame,
range, normal/exception effects and SSA read/write correspondence. The later
value-domain check remains mandatory, including potentially absent slots that
contain Hole. The checked argument fact is retained in SemanticProof. No owner
still produces `kArgumentOwner`, including zero-declared-parameter functions.
This is not source publication: runtime count/tail binding,
snapshot factory/AST and callable-specific source lowering are still missing.
The explicit entry premise does not exempt any decoded caller from argument
value checks or certify arbitrary VM-internal invocations.

The shared `OperandView` now accepts snapshots and owns only their compact
window. It independently checks fixed/range inputs, the accumulator write and
the allocation/exception effects. Each slot is an `Argument` protocol use with
its original 32-bit SSA read ordinal, not an eager JS call operand. Actual count
and tail remain frame inputs, not invented scalar uses. Build/index access are
O(1); use expansion stays under the importing pass's existing work/memory budget.
This adapter does not authorize dropping potentially absent slots or admitting
Hole-bearing inputs. The same decoded method is tested through semantic lifting,
SSA and operand/slot correspondence before malformed-effect mutation checks.

The independent input checker allocates nothing and uses one monotone SSA-read
cursor. Its cost is O(I + R + S log V), where I is instruction count, R is visited
read occurrences, S is snapshots and V bounds indexed SSA write lookup. Every
step shares the verification work budget; it never performs a graph walk for
each argument. File-backed ordinary/arrow fixtures check owner binding, absent
owners, wrong identities/targets/frames and distinct same-target creation PCs.

Rules 38 import snapshots as `ValueArgumentSnapshot`, retaining the checked
`ArgumentFrameFact` in `ValueProgram`. Import rechecks the original instruction,
effects and SSA inputs under its work budget; independent correspondence checks
the fact, declared slot count, protocol read ordinals, origins and exceptional
exits. Rewrite verification requires the frame fact to remain unchanged, and
snapshot allocation is explicitly non-erasable even if effect fields are cleared.
Simplification retains separate snapshot occurrences and their conditional uses.
At that revision the file-backed ordinary/arrow fixtures reached value IR, but
evaluation/AST construction still rejected snapshots: count/tail source bindings and
the exact object factory have not been implemented. This is intermediate-layer
evidence, not executable source coverage.

Rules 39 extend expression scheduling with typed `ConditionalArgument` inputs.
Declared slots keep their protocol-read identity, language-domain check and
original ordinal. Their producer trees cannot be inlined into the conditional
read; even a pure copy wrapping a getter must remain evaluated before the
snapshot. The independent schedule checker rejects forged movement and missing
invocation facts. Snapshot allocations still participate in the ordinary ordered
effect sequence; neither CFG nor scope structure is replaced. Count/tail facts
travel in `EvaluationPlan`, but do not yet name a source binding. The next AST
must implement `ordinal < actual_count` and attach the actual invocation tail;
an eager JS array is not authorized by the plan. File-backed ordinary/arrow
fixtures reach this schedule and still fail AST construction explicitly.
Additional checks remain O(operations + input occurrences), use constant-size
per-shape/fact storage and share existing pass budgets.

## Owned Source Snapshot Route (Rules 40)

`ArgumentSnapshotExpr` now owns a streamed range of conditional current-slot
leaves and a `FunctionOwnerId`. The ordinary expression forest, structure/flow/
operand verifiers, ABI census and printer consume this node. The node requires
strict source, an owner with its own native `arguments` binding (not an arrow),
unchanged declared arity, and the V2 resource-exhaustion exclusion. It does not
introduce a new CFG, source scope, constructor writer or register-text fallback.
Pure/effectful slot producers remain at their verified schedule positions.

The native arguments object is an unexposed invocation carrier, NOT the returned
snapshot. Strict source bindings cannot shadow `arguments`; direct eval is not
introduced. For each declared slot the printer emits the original current-value
leaf only under `arguments.length > ordinal`. Extra tail values are taken from
that invocation carrier by the factory, never invented from declared arity.
This preserves missing arguments versus explicitly supplied undefined, including
when a parameter slot has since been overwritten. A returned snapshot never
aliases the carrier or a previous snapshot.

Publication requires caller-supplied `HostArgumentSnapshotBinding` with typed
`ArgumentSnapshotAbi::kV7CurrentSlots`. This is an embedding-host contract, not an
ABC-derived fact or a CLI helper-name switch. The immutable lexical factory takes
the carrier and a dense declared-slot array, returns a fresh pinned-realm arguments
object with exact indexed/length/iterator/caller/callee descriptors and key order,
and never mutates, retains or exposes its inputs, invokes user code, or throws
other than excluded resource exhaustion. Missing/colliding bindings refuse before
publication; family ancestry checks use the existing scope indexes. Printed
positions retain the binding name, ABI and source-contract version. Ordinary
global, realm and Symbol references cannot accidentally resolve to this binding.

File-backed read, slot-write, repeated-snapshot and branch fixtures pass the same
decode -> semantic/SSA -> value -> evaluation -> AST -> printer pipeline and
seal method receipts only after emission. Tests execute emitted JS with an
explicit test-only factory, check actual counts below/equal/above declared arity,
identity, descriptors, property order and retained iterator identity, and verify
original snapshot PCs against generated ranges. The fixture uses a zero-formal
native arguments object with an empty parameter map, removes its configurable
callee, and installs caller then callee; using a strict native arguments object
would retain the wrong string-key order. This fixture is not an Ark VM differential
run or a production host adapter.

The source acceptance matrix also contains a finite backedge fixture at declared
arities 2, 200 and 201. A snapshot is stored into a parameter on each iteration,
then included in the next snapshot. Executed output checks the nested object
identities and current boolean slot on both iterations, independent calls and
construction. Factory-call counts verify all three allocations even when no
actual arguments expose the intermediate objects. Both original snapshot PCs
retain generated ranges. This uses the existing CFG/phi/loop AST route without
a loop-specific snapshot writer; the 20 file-backed variants are synthetic, not
additional application-corpus or Ark VM differential evidence.

## Literal Defaults and Method Owners (Rules 41)

The shared family builder now projects both SSA-value literal defaults and
lexical-cell literal defaults through their existing sealed `DefaultBody` proofs.
Value-default projection formerly existed only in the single-function route;
the family route now performs it before environment rebuilding and child capture
rebinding. Migrated operations are explicit residual no-ops for both storage
forms. Their original PCs remain owned by the signature proof, not labeled as
erasable original control/effects. Surviving instruction-site indexes are
retained for module/capture rebinding; original value correspondence is not
copied to the initialized-formal interpretation.

`DefaultValueProof` admits a merged post-default value as a conditional snapshot
protocol input only for a typed `ValueArgumentSnapshot` with the invocation fact
and matching declared count. The original pre-default entry still cannot escape
into the body. Other protocol consumers, incorrect roles/counts and absent facts
reject. The source carrier still supplies the original actual argument count
and extra tail; moving the default into a formal must not invent a supplied
argument when the caller omitted it. Mixed value/cell prologues, effectful
initializers and rest signatures are not authorized by this extension.

Three additional file-backed class-method variants cover a plain parameter,
integer `-1` default and equivalent negated numeric default. Each follows
`PreparedFamily::PrepareClass` through emission receipts and original-PC ranges,
then executes as a real class method. The 23 total synthetic variants check
method name/length/nonconstructibility, omitted versus supplied undefined,
non-undefined values, extra tail and host-binding refusal. This does not turn
synthetic fixtures into application-corpus or Ark VM differential evidence.

## Arrow Invocation Carriers (Rules 42)

`FunctionTree::invocation` separates the recovered source form from the original
`CallableKind`. An arrow containing a snapshot can select `kArrowMethod`: the
unified printer emits a computed-name concise method and extracts it from its
unexposed object. The result has its own native arguments carrier, the original
name and declared length, no own prototype, and no construct capability. It is
NOT a change of the ABC callable kind to an ordinary function. Non-snapshot
arrows retain arrow syntax.

The structure checker requires a strict arrow, creation-proven name, an actual
snapshot and the existing resource-failure exclusion. It forbids `ThisExpr` for
this form. Independent ABI verification continues to reject dynamic receiver,
LDTHIS and new.target observations; lexical captures remain explicit symbols/
cells owned by the original enclosing function. The current source AST cannot
express direct eval or super, so the method's unexposed home object cannot be
observed. Adding either feature must re-audit this lowering, not simply reuse
the carrier. LDFUNCTION self identity retains its existing private creation
binding instead of referring to the temporary object or its method name.

The form still requires the exact snapshot factory. Rules 43 additionally
accept the user-approved [V3 recovered-text contract](../source-contract-v3.md)
as the text permission. V1/V2 do not gain this permission implicitly: their
destination still needs explicit `CallableTextPolicy::kRecovered`, otherwise
`kCallableText` refuses before output. Positions record `invocation_form: arrow_method`
and `callable_text_policy: recovered`; the scope check is repeated on embedding.
This policy permits different Function.prototype.toString text, not differences
in calls, parameters, values, effects, constructibility or capture ownership.
The user approved the engine-wide textual boundary on 2026-09-27. V2's meaning
remains unchanged; no CLI host capability was introduced.

Pinned evidence: `js_function_kind.h` and `js_function.h` distinguish arrow
kinds from constructor/prototype kinds; `builtins/builtins_function.cpp`'s
FunctionPrototypeToString obtains embedded debug source (or "Cannot get source
code"), so exact text reflection must not be inferred from the call ABI. This
is also a cross-cutting limitation of reconstructed source, not solved for other
callables by this carrier. Node execution below is not an Ark differential run.

The synthetic matrix now includes the ordinary and arrow variants of read,
slot write, repeated snapshots, branch, loop and LDFUNCTION self observations,
plus the three class-method variants (51 total). It checks object identity,
actual argument counts/tails, name/length, own keys/prototype, caller/arguments
throws, bind constructibility, exact snapshot PCs and changed-destination refusal.
Forged carrier kinds, missing names and dynamic receiver observations refuse.
Selection and structure validation add bounded linear expression scans and one
typed field per tree; ABI and destination checks reuse their existing passes.
No CFG, dominance, scope-parent or capture-ownership relation is rewritten.

Still open: default/rest and captured-family combinations for arrow carriers;
broader method/constructor/async/generator and mixed-default/rest combinations;
host integration in actual applications and multi-application runtime evidence.
Without the explicit factory the normal CLI continues to refuse snapshots.
The opcode family and group 1 are not globally complete.

1. **Frame inputs.** Build a compact, validated argument-window plan from the
   actual FrameLayout and callable creation occurrence. Keep actual invocation
   count and extra-argument tail as typed frame inputs, distinct from declared
   register slots and source formal count. Missing extra-argument evidence must
   reject, not default to the declared count. Validate canonical hidden-prefix
   positions and checked arithmetic before constructing the plan.
2. **Semantic lifting and SSA.** Introduce a typed snapshot/allocation operation,
   with ArgumentsRead and Allocation effects and normal-only acc definition.
   Every declared parameter it may consume must reference the SSA value at that
   PC. Count/tail ownership remains a separate explicit frame-input contract,
   like environment ownership, not a fabricated scalar register. Tail values
   must be validated as language arguments under the execution contract.
3. **Operand representation.** Extend the compact operand view, not the fixed
   scalar operand array. CallRange counts remain 16-bit, while FrameLayout
   parameter counts follow 28-bit runtime fields. Shared SsaRead/InputRead,
   operand correspondence and guarded-read lookup now use 32-bit occurrences;
   the largest encoded receiver call no longer exceeds their representation.
   The separate 32-bit RegisterRange now represents frame-window may-reads;
   encoded calls convert their smaller ranges explicitly without widening
   their bytecode count contract.
   Budget O(number of admitted read occurrences).
   Actual count controls which declared slots are present. Until that condition
   is proved, may-reads cannot be discarded to make Hole-bearing cases pass.
4. **Independent verification.** Recompute window/owner and read correspondence
   from the original instruction and frame. Verify the actual-count predicate,
   current-slot values, tail, allocation occurrence and normal/abrupt effects.
   Two executions of a snapshot, including on a backedge, allocate distinct
   objects. No CSE, copy folding or DCE may merge those observable identities.
5. **Source AST and publication.** Add a typed argument-snapshot expression and
   frame-input binding plan. Do not print bare `arguments` or a rest array.
   A host-proven snapshot factory is an admissible route to the exact arguments
   object descriptors/intrinsics; any such capability must be explicit, immutable
   and recorded in emitted evidence, with indexed scope checks. It cannot be a
   mutable global helper lookup or an invented CLI-name capability.
6. **Callable integration.** For ordinary strict owners, an unexposed entry
   arguments carrier can preserve count/tail while snapshots consume current
   parameter SSA values. Never expose or mutate that carrier as a snapshot.
   Arrow owners need their own invocation carrier, not the enclosing function's
   lexical `arguments`; solve this together with signature/name/length and
   constructibility preservation. Derived, async and generator owners must use
   their existing phase/capture protocols. Do not create constructor-only writers
   or waive these remaining owner obligations.
7. **Source evidence.** The same file-backed method must traverse decode,
   semantic effects, SSA, value IR, scoped AST, independent verification and the
   unified printer. Preserve original PC spans and publish the frame/factory
   requirements. A valid operand inventory or rejected fixture is not completion.

## Acceptance Matrix

- Actual counts zero, below/equal/above declared count; optional hidden ABI slots;
  both missing and supplied undefined arguments; extra-argument tails.
- Parameter-slot writes before/between snapshots, including overwritten defaults;
  two snapshots compare unequal and mutating the first does not change the next.
- Rest-copy interactions; no rest-signature rewrite while a snapshot still reads
  original slots unless the new frame-input proof explicitly discharges it.
- Indexed/length descriptors, caller/callee throws and accessor identity,
  prototype/brand, iterator identity after public prototype mutation.
- Branches, loops, normal/exception edges, closures and independent invocations;
  ordinary/method/arrow/derived/async/generator ownership tested separately.
- Internal-marker operands, wrong owner/window/count, missing capabilities,
  shadowing and malformed encodings reject with typed failures.
- 200/201 and geometric read counts, 16-bit occurrence boundary, budget exhaustion;
  no quadratic per-variable ancestor scans or unbounded argument expansion.

Keep production inputs ABC-only. Generated-JS checks and a small test oracle do
not replace Ark VM differential evidence, and neither proves the process RSS cap.
