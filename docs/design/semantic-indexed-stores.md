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
This was the gate before the array-construction fold below.

## Dense object-array construction

The shared AST now recognizes an empty array saved to a register followed by
contiguous primitive object-buffer creations, spill/reload pairs and own-index
definitions starting at zero. The entire interval must stay in one normal CFG
block; phased exception SSA is excluded. No calls, array reads, extra aliases
or control flow may intervene. Object buffers must still pass the printer's
primitive-only validation; an unsupported buffer rejects the whole artifact.

Typed `SourceArrayCapture` nodes retain each element at its original store PC.
The final `SourceArrayCommit` emits one native array literal into the original
receiver register. Object allocation order, accumulator and temporary register
results remain unchanged. Array allocation is deferred across only these
unobservable operations under the existing no-OOM/debugger contract. Partial
matches commit their completed prefix before leaving later operations untouched;
this does not grant general own-property assignment permission.

The original empty-array PC maps to the final commit span; every instruction
retains its source anchor. Element temporaries use reserved generated names,
checked against local and enclosing source bindings. The fold scans instructions
linearly and stores O(E) element IDs. Geometric vector growth admits old plus
new capacity, and all retained plans share the existing AST byte budget.

ABC-only inspection of constructor 10501 now produces `PrintedSource` through
the shared pipeline, including `[$element97, $element101]`, subsequent instance
property initialization and 154 PC spans. Record entry 9915 passes class creation
at PC264 and next refuses method assembly at PC295. This is not complete record
publication or executable-equivalence evidence; the batch record path has not
been migrated to this record printer.

Lifting and AST lowering add constant work/storage per instruction; existing
SSA/region/AST budgets account for retained structures. Both production CLIs
compile and read-only review completed. Tests/RSS remain paused. Deferred
regressions include all encodings/bounds, absent cache slot, unchanged
accumulator, proxy failures, inherited setters, class enumerability, array
length limits and descriptor-aware source admission.
Also deferred: dense prefixes, receiver/temporary alias rejection, interrupted
construction, generated-name conflicts, source span relocation and plan-budget
boundaries. No test, generated-JS execution or RSS measurement was performed.
