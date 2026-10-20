# Captured-cell initialization and native scope

Checkpoint: 2026-10-20, baseline `adec8da6`. No new source contract.
Only an unmet exceptional capture-initialization obligation uses the new path.

## Diagnosis from original ABC

`CaptureCellError::kInput(3) > CaptureError::kScope(1) >
ScopeError::kInitialization(3)` means that closure creation is not covered by a
completed Language store to the resolved parent cell. It does **not** identify a
per-iteration environment. PCs below are original unless marked working.

| Record | Originating capture/access | Creator, activation and initialization | Missing proof |
|---|---|---|---|
| o26/4515 | m4696 ordinal3, depth0/slot2 `isPlaying`; load PC242, HoleGuard244, store254 (working251) | m5540 module entry: NEWLEXENVWITHNAME PC0, 30 slots; creation PC60 produces m7782 (`onAudioInterrupt`), which creates m4696 at PC49; first slot2 store PC618; slot1 callback initialized PC303 | Language **write** obligation through an async child/implicit handler; late mutable initialization. Not a read-only Guarded deferral. |
| o36/11209 | m11413 ordinal0, depth0/slot0 `this`, load29, no HoleGuard | m12561: named one-slot environment PC0, receiver store12; an unrelated index loop; callback creation187 | Completed-store availability across a cycle when there are no local reads to request cyclic initialization facts. Not a per-iteration capture. |
| o37/20175 | m20373 ordinal0, depth0/slot0 `data`, load46/HoleGuard48 (working43/45) | m20457 async `bind`: two-slot environment PC0 (`data`, `this`); this store10; callback creation364 (working355); data store412 (working382), only after await success | Deferred TDZ declaration in the protected try body, with exceptional activation and descendant enclosure. |
| o38/25106 | m25320 ordinal0, depth0/slot0 `data`, load46/HoleGuard48 (working43/45) | m25600: same two-slot activation PC0; this store10; callback creation374 (working365); data store424 (working394) | Same proof as o37. |

The largest common group is the RpcTool pair (2). They are neither catch
parameters nor loop-body lets. The VM hoists their physical environment to
function entry; native `let data = await ...` belongs to the try body's lexical
scope. The callback can escape and execute before success or after rejection.
It must retain a ReferenceError until the original store commits. No undefined
initialization, copied value, or exceptional-parent initialized premise works.

Independent source lookup, **after** the bytecode diagnosis: UsbSerialDriverDemo
`RpcTool.ets:149-155` and ScsiPeripheralDemo `RpcTool.ets:175-181` both contain
`let data = await deviceManager.bindDeviceDriver(this.deviceId, async (...) =>
{ ... data ... })`. Sources are oracle material only, never engine input.

The previous flags differ for o26: their earlier refusal is a Language store
in m7700 PC112 (working88), slot5 `writeDataCallback`, forwarded at m5540 PC37.
o36 uses Language rather than SourceAccess under the previous flags. The two
RpcTool reads are Guarded under previous flags, SourceAccess under V5.

Pinned runtime basis: `ets_runtime/ecmascript/object_factory.cpp:2708-2716`
initializes lexical slots with Hole. `interpreter/interpreter-inl.cpp:6118-6133`
STLEXVAR stores acc into the resolved parent environment/slot without a TDZ
check, which is why Language writes before initialization remain refused.
`interpreter-inl.cpp:4107-4121` dispatches normally for non-Hole and routes
Hole to `ThrowUndefinedIfHole` and the original exception handler. These paths
are under `references/openharmony/arkcompiler/`. Native TDZ preserves the
exception constructor and traces; VM-specific messages are not a Node oracle.

## Exceptional deferred activation

`ExceptionLocals` retains a typed, sorted unique-store index, keyed by the
original environment/slot's `LocalCellId` **inside that verified owner only**.
Each fact contains the actual store instruction and the original allocation
Commit's complete exception-CFG dominator interval. Every creation query first
resolves its genuine environment/slot; it cannot equate a child CellId or name
to parent storage. Allocation Commit must dominate both creation Before and
store Commit. Store-domain and transitive Language-write obligations remain.

`VerifyExceptionDeferredInitializations` independently rederives the index from
immutable flow/environments, including allocation identity and phase preorder.
The phased file reader invokes it once per method, not once per capture.
Normal successful plans and strict Language/Defined obligations are unchanged.
Guarded reads may defer; SourceAccess may defer only under the already-approved
`CaptureReads() == kSourceTdz`. No new premise or guard elimination is added.

The working verifier independently rebuilds phase-aware value-flow dominance.
The real allocation Commit, not its Before event, dominates the unique store.
Throwing paths bypass Commit; a direct reset/additional store rejects deferral.

## Native private scope

The first store remains the initialized `let` declaration. An allocated cell
may use the store's narrower native scope only if:

1. It descends from the allocation's independently reconstructed source scope.
2. No loop lies between the two scopes. A prepaid prefix count answers this
   without per-cell ancestor walks. Thus the new source entry occurs at most
   once in the original environment activation; a loop cannot refresh an old
   cell. Allocations already inside a loop retain that loop's identity.
3. Every original local access and **every** resolved descendant demand lies
   inside that native scope. A closure created outside it refuses, even if it
   happens to read the cell only later. No scopes default to function scope.
4. Original stores, exception dispatch and cleanup are rechecked by common
   ExceptionControl, TreeFlow and recovery. The native declaration carries the
   exact original first-store expression; its binding is TDZ before that point.

Only this cell moves to the smaller source scope. The initialized receiver
cell stays at function scope. Before entry into the narrower scope there is no
cell read or capturing observer, so delaying its unobservable native allocation
is equivalent. If await rejects, an escaped closure still captures the lexical
binding in TDZ; the catch does not manufacture an initialization.

Builder and TreeFlow recompute the scope/activation proof separately. All
expression, capture-name, scope-enclosure, event and recovery checks remain.
The common printer emits the result; no printer changes are involved.

The shared exception dominance/unique-store indexes and scope Euler intervals
cost O((V+E) log V + D log C) time and bounded O(V+E+C+D) storage; environment
ancestry is fixed-width indexed. `D` is actual descendant demand count, `C`
cell count. Every new vector is admitted before allocation. Exhaustion refuses.

## Acceptance checkpoint

See the round entry in status.md and `build/review/capture-scope/` for final
build/test counts, cohort deltas, exact budget witnesses and SDK disassemblies.
The pair's `bind()` bodies pass the whole existing source/recovery pipeline.
Other member refusals are reported rather than fixed in this slice.

### SDK comparison

`tests/tools/capture_scope_roundtrip.py` reuses grouping_bytecode.py's compiler,
SHA-256 and unnormalized assembly reader. It recompiles the original independent
sources and engine output at both O0 and default, preserving registers,
branches, handlers, literals and metadata in all retained diffs. A real Acorn
AST adaptation rewrites only identifier-valued string export aliases on either
side, because this SDK rejects that JS export syntax. It changes no binding or
export identity. Original input fixtures are not altered.

Six output compilations pass, with 18 methods compared: six module-entry
methods are byte-identical, 12 function/callback methods differ. The six record
comparisons are **not** claimed byte-identical. Every diff falls into these
existing recovery transformations:

- `log` is a recovered mutable captured local initialized at the first body
  store rather than a captured parameter initialized before ASYNCFUNCTIONENTER.
  Its own store has no user effect or exceptional exit; the implicit async
  catch starts earlier in the recompile, covering that nonthrowing store.
  Additional compiler HoleGuards on `log` are redundant after its proven
  Language store. O0 also emits the corresponding register copies; default
  eliminates most copies. The private cell identity is unchanged.
- Recovered cell names replace `data`/`self` in the HoleGuard string operands.
  This affects TDZ message spelling, not the exception constructor, throw point
  or catch destination, under the existing guarded-read/source-TDZ boundary.
- Equivalent if/else emission complements strict equality and exchanges its
  taken/fallthrough arms. A path that returned or threw originally still does;
  source ordering on each executable path is unchanged. O0 may add an
  unreachable jump following a return. Cache operands are renumbered because
  the instruction layout changed; property/argument identities are unchanged.

No extra parameter, function, import, export, module request or environment
slot is introduced. Async enter/await/resolve/reject protocols and each real
callback capture relation remain present. Node separately checks observable
values, early/late/rejected TDZ and retained activation, exception constructors,
effect/microtask order and receiver identity. This is semantic round-trip
acceptance, not register/branch normalization or an Ark runtime execution claim.
