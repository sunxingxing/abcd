# Lexical Environment Operations

The typed `LiftLexicalInstruction` boundary covers sixteen encodings: three
modern loads, three modern stores, three deprecated register stores, four
environment allocations, two pop encodings and deprecated current-environment
load. It does not emit closure source or traverse runtime environment objects.

Pinned `ecmascript/interpreter/interpreter-inl.cpp` reference behavior:

- NEWLEXENV and named variants allocate a child environment,
  install it as current and write its internal identity to the accumulator.
  Named variants retain a literal-array pool slot, not a direct entity offset.
- Deprecated LDLEXENV loads current environment identity into the accumulator.
- POPLEXENV variants install the parent without changing acc.
- LDLEXVAR follows exactly `level` parents, then loads `slot` to
  acc. Level zero selects the current environment.
- Modern STLEXVAR writes acc into the addressed slot and leaves
  acc unchanged. Deprecated variants use an explicit register
  as the value source, also preserving acc.

`FastRuntimeStub::NewLexicalEnv` connects the parent and global environment.
Both `ObjectFactory::NewLexicalEnv` and `InlineNewLexicalEnv` initialize fresh
slot storage to Hole. Therefore neither undefined-filled JS arrays nor eager
ordinary-variable reads are generally equivalent. A load does not itself
perform a JS TDZ check; explicit hole-check instructions must be incorporated
before source declarations and accesses can be recovered faithfully.

IR alternatives encode loads/stores/creation/pop/current separately. Addresses
use typed depth and slot fields; store sources distinguish accumulator from
register. Allocation counts and optional scope slots are retained without
allocating per-slot IR. Bounds checks validate encoded operand ranges and
register windows, not the dynamically selected environment's size or ancestry.

Future closure binding must account for control-flow joins, loop-created fresh
environments, captured environments surviving pop, and shared/sendable context.
Do not rename `(depth, slot)` globally as if it were a unique variable identity.
Do not expand a 65,535-depth address by walking a chain per use: environment
resolution needs indexed ancestry or another bounded scalable representation.
This lifting primitive itself is constant time/storage independent of depth,
slot count and method size.

## Failure cause in the common semantic model

The checked working-IR topology publishes `normal_after` for each live
operation with successful completion. Allocation selects the fresh environment;
pop selects its parent. Before and exceptional paths retain the old state.
Removed slots can have a topology position but have no successful execution.
Normal and phased verifiers check these states independently against ancestry,
phase timing and successor agreement. The additional table costs O(operations)
space and work, with parent queries bounded by the existing ancestry index.

`CellEvaluation`, source lifetime checks and retained `OriginalFlow` share this
immutable topology. Cell verification checks the supplied instance instead of
replanning it and thereby overlooking stale facts. Local source-cell identity
uses the allocation's checked successful EnvironmentId and slot; incoming
cells use captured depth and slot. These are environment families in one owner,
not proof of equal dynamic allocation counts or source scope entry timing.

`SemanticEffects::exception_cause` records a separately proved cause; an
allocation-effect bit alone never proves that all failures are resource failures.
The pinned adapter classifies ordinary `PushEnvironment` as resource exhaustion.
Other operations remain unclassified until their own semantic contract supplies
the fact. The claim assumes the existing validated bytecode/metadata and ordinary
environment contract, not malformed metadata or observable VM/debugger internals.

Evidence at the locked ets_runtime commit:
[interpreter handlers](https://github.com/openharmony/arkcompiler_ets_runtime/blob/f04900cf951c66c2ea18b2bab5b591d5336c34b9/ecmascript/interpreter/interpreter-inl.cpp),
[runtime allocation helpers](https://github.com/openharmony/arkcompiler_ets_runtime/blob/f04900cf951c66c2ea18b2bab5b591d5336c34b9/ecmascript/stubs/runtime_stubs-inl.h), and
[scope metadata extraction](https://github.com/openharmony/arkcompiler_ets_runtime/blob/f04900cf951c66c2ea18b2bab5b591d5336c34b9/ecmascript/jspandafile/scope_info_extractor.cpp).
The call chain allocates internal storage, links its environment, and for named
forms reads scope metadata. It does not perform a language conversion, property
getter or user call. This is the basis for the failure classification; the
interpreter still installs the new environment only after success.

Normal/phased import, independent correspondence and rewrite checks preserve the
cause. Recovery distinguishes a mapped throw, a contract-excluded resource
failure, and missing correspondence. Exclusion requires the explicit existing
source resource policy and grants no physical allocation point, normal result,
source initialization or binding-lifetime proof. The current and trial owners
consume the same failure product. This completes the failure-cause path only;
the raw allocation action's source/lifetime correspondence remains separate.

Tests decode every supported encoding, distinguish modern/deprecated sources,
check packed-nibble ordering, optional scope references, register bounds,
malformed operands, 200/201 and maximum-width counts. These structural tests
do not establish historical runtime equivalence, TDZ/source behavior, captured
binding identities or complete per-record decompilation.
