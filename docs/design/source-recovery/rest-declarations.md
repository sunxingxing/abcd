# Private declarations before native rest

The native-rest proof's former whole-prefix effect check rejected MockKit
(o32/53454) `findProperties` m54264: PC0 creates a lexical environment, PC14
creates helper m54291 `getProperty`, PC23 stores that helper in the fresh local
cell, and PC25 executes COPYRESTARGS(1). The metadata's length is 1; the failure
was `RestOwnershipError::kOrder`, collapsed to `SyntaxResolutionError::kParameters`.

The old pure-transport proof remains the first path. Only when it fails the
order condition does the new proof classify private declaration events. It
borrows the real input owner's already verified `ValueEnvironments`; it neither
re-decodes the creator nor compares environment IDs between owners. A lexical
store must resolve to an allocation in this prefix, with a Language stored
value. Incoming stores, lexical loads/guards, calls, externally observable
accesses/writes and argument snapshots remain refused. The reverse CFG cut is
closed against outgoing side paths before rest; an early return or escape
cannot hide on a path that misses the allocation. Existing repeated-allocation,
physical argument-window and raw scalar-entry use checks remain mandatory.

The pinned allocation semantics are:

- [interpreter DEFINEFUNC](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L5080)
  calls [SlowRuntimeStub::DefineFunc](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/slow_runtime_stub.cpp#L951),
  then [RuntimeDefinefunc](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/stubs/runtime_stubs-inl.h#L2485):
  immutable pool lookup, function allocation, module/environment/home-object
  attachment and length initialization. It never invokes the function body or
  performs a source property lookup. Actual target kind and captures still
  require the existing file/family proofs. Method definitions with an acc home
  object are not declaration instantiation.
- NEWLEXENV has the existing typed resource-exhaustion cause and successful
  environment transition. It installs private Hole-filled storage, not a
  language value. Rest syntax remains subject to the existing resource-failure
  exclusion, so only allocation/resource timing is crossed.
- [COPYRESTARGS](../../../references/openharmony/arkcompiler/ets_runtime/ecmascript/interpreter/interpreter-inl.cpp#L6039)
  reads the actual invocation window. Its original index, scalar entries,
  physical writes, one-allocation-per-invocation and fresh-array identity are
  not inferred from declaration layout.

`RestDeclarationEvent` records the environment/function/cell-store semantic
roles. `VerifyRestOwnership` re-derives membership, storage, effects, frame and
argument uses from the immutable graph/body/environment owner and compares
those events with separately charged comparison work. Syntax resolution calls
it before marking the rest parameter. Residual operations and capture targets
retain their original order; the ordinary tree/ABI/recovery checks remain
mandatory. `SyntaxRestFailure` retains the real lower cause and available PC.
No async normalization/prelude admission changed.

Indexes and event buffers are admitted before allocation. Traversals are
linear, with bounded uint32 ancestry resolution per lexical store; original
SSA def-use transport is visited once. No per-helper whole-CFG walk or
fixed-length/opcode-adjacency template is used. Unit tests exercise copy/register
and block-split variants, early invocation/escape/read/return, physical window
mutation, forged event role/coverage, wrong depth, and exact planner/verifier
byte/work boundaries at 200/201/400/800.

## Evidence and remaining boundaries

The positive real es2abc fixture exports private helpers, helpers capturing an
ordinary parameter, and a recursive helper. Engine-output Node checks compare
48 ordinary-call scenarios, `.length === 1`, missing versus explicit undefined,
fresh arrays, per-call isolation and getter/exception traces. Recompiling all
three emitted functions preserves COPYRESTARGS index 1 and two formal slots.
Compiler identity/options/hashes and the exact export-alias adaptation are
recorded with `tests/integration/fixtures/rest_declarations`.

The exact requested captured-rest example is a separate negative fixture. Its
prefix stores the physical last-argument scalar in a captured cell, then later
replaces it with the array. Existing `kArgumentUse` remains: proving that first
cell value dead is not a consequence of private declaration instantiation.
This fixture is not claimed as a successful engine-source differential.

MockKit's rest refusal is gone. Under --language-source the next published leaf
is `FamilyEnclosureError::kStructure` with no published method/PC; a read-only
scratch probe locates `TreeStructureError::kBinding` in m53795 (`f`), family
member 1. Under previous flags the next leaf is
`ArrayInitializationError::kContract` in m54291 (`getProperty`), with no PC.
Neither downstream obligation is changed in this slice.

The existing `for_in_evaluation_cli_test` now asserts MockKit's exact next
leaf per contract (array contract in the helper versus enclosure structure),
rather than the old syntax/rest-order refusal. Both paths still require
manifest/`abc_semantic --record-source` equality; no source or status assertion
was relaxed.
