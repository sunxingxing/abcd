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
The file-backed ordinary/arrow fixtures now reach value IR, but evaluation/AST
construction still explicitly rejects snapshots: count/tail source bindings and
the exact object factory have not been implemented. This is intermediate-layer
evidence, not executable source coverage.

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
