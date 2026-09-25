# Indexed own-property definitions

`IndexedWrite` now enters the common semantic IR and `SourceIndexedStore`
retains its receiver, index and accumulator value in the shared AST. This
reuses the existing validated `STOWNBYINDEX` decoder/register effects instead
of introducing a constructor-specific writer. Both cache operand widths and
the wide index encoding are supported; the optional cache slot distinguishes
wide absence from encoded slot zero. Cache state is format metadata only.

## Runtime boundary

Pinned OpenHarmony-v7.0-Release `ets_runtime` sources:

- `interpreter/interpreter-inl.cpp:5898-6037`: receiver register plus
  accumulator input, normal/abrupt accumulator preservation, own-index fast
  paths and runtime fallback.
- `stubs/runtime_stubs-inl.h:790`: own descriptor with writable/configurable
  flags; class constructor/prototype definitions are non-enumerable. The
  original decoder comment claiming unconditional enumerability was corrected.

The operation conservatively records heap reads/writes, user code and
allocation both normally and before an exception. It may throw and defines no
new SSA register value. The full uint32 encoded index is retained; the maximum
uint32 property key must not automatically be treated as an array-length index.

Ordinary `receiver[index] = value` is not an equivalent general lowering:
it can invoke inherited setters and uses different descriptor rules. The
printer currently returns typed `kOwnProperty` until array-construction or
own-descriptor provenance is available. It does not emit a call through a
mutable global intrinsic as a substitute. Existing alias/escape observation
remains conservative; lifting the operation grants no private-allocation fact.

## Pasteboard evidence

ProgressBar constructor 10501 formerly failed semantic lifting at PC249.
It now builds 154 instructions, 7 blocks and 152 SSA values. The actual class
owner reaches the shared printer and refuses `kOwnProperty` at PC249, so
the constructor's semantic proof, regions and AST are no longer the gate.
Instruction inspection retains receiver register 13 and indices 0/1 at
PC249/262, with cache slots 26/29 and their exceptional register snapshots.
The containing record still refuses class creation at PC264; no complete
constructor or record is claimed.

Next, prove the unescaped empty-array construction and its element evaluation
order before folding these definitions into a native array literal. Do not
change array identity, expose partially initialized aliases, duplicate object
construction, or change getter/call/exception ordering to improve coverage.

Lifting and AST lowering add constant work/storage per instruction; existing
SSA/region/AST budgets account for retained structures. Both production CLIs
compile and read-only review completed. Tests/RSS remain paused. Deferred
regressions include all encodings/bounds, absent cache slot, unchanged
accumulator, proxy failures, inherited setters, class enumerability, array
length limits and descriptor-aware source admission.
