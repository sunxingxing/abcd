# Field Read Composition

## Final-AST Liveness

The common function pipeline computes accumulator may-liveness after receiver
recovery and snapshot cleanup. Original SSA counts cannot describe all reads
after retargeting, so this analysis consumes emitted AST operations instead.
Each operation has a typed first-access classification: none, read, or write.
A read wins over a write within the same operation. Unknown protocols count
as reads; they are never interpreted as absent accesses.

For each normal block, its first nonempty access determines whether it reads
or kills the incoming accumulator. Reverse adjacency propagates live-in only
through blocks with no access. A block changes from not-live to live at most
once, so loops converge without repeated graph walks. A backwards statement
scan then records live-after using the union of successor live-in values.
This is conventional backwards may-liveness specialized to one VM slot, not
a new inference of source variables or a dependency on opcode strings.

Staged calls and array owner intervals are entirely marked as reads, including
their flat child statements. Super argument intervals are also barriers.
Constructor/fused/class/literal owners remain conservative. The exception is
a typed conditional predicate: its fused origins only relocate PC spans; the
printer reads its condition/input, not the removed producer instructions.
Catch regions, phased SSA, branching argument regions and resume edges are
not optimized by the field-composition caller.

Time is O(I+B+E) for instructions, blocks and edges. Storage is O(I+B+E),
admitted before allocation from the common AST owner's remaining scratch
budget. Capacity is checked after allocation as well. Allocation/limit failure
reports typed ResourceLimit; no unbounded queue or per-candidate CFG search
is used. Returned access/live-after arrays remain charged while folding.

## Composition And Ordering

The first supported slice is two adjacent final statements in one block:

```js
$acc = source.input;
target.output = $acc;
```

Both bases must be private registers and both keys fixed names. If the
accumulator is dead after the store, the common AST carries a MemberExpression
as the typed FieldValue and emits `target.output = source.input`. Bases and
keys need not be equal. No getter is duplicated or deleted. Calls, computed
keys, lexical cells and expressions with their own source owners are excluded.
Own-property stores retain their separate provenance and are not folded here.

The only evaluation moved before the RHS is reading the stable private target
register and forming a fixed-name Reference. Neither invokes user code.
The RHS property GetValue still occurs before the target PutValue, including
setter/proxy effects and throws. See ECMA-262 2022
[named property references](https://tc39.es/ecma262/2022/multipage/ecmascript-language-expressions.html#sec-evaluate-property-access-with-identifier-key)
and [assignment evaluation](https://tc39.es/ecma262/2022/multipage/ecmascript-language-expressions.html#sec-assignment-operators-runtime-semantics-evaluation).
This does not require the two objects to be distinct or add a host assumption.

The removed producer retains its original PC as an AST anchor. The store's
fused_read/read_distance maps that PC to the generated assignment through the
existing span relocation mechanism. The printer observes RHS registers through
the same typed binding APIs, and its slot census drops an unused acc declaration.
No source-text postprocessing is involved.

## Verification Boundaries

Both production CLI targets build. ABC-only pasteboard export removes 27
accumulator assignments and 11 accumulator declarations across the six pages
and ordinary callbacks. PasteBoard's ten initial-value assignments now directly
read params properties. Publication remains eight incomplete sources, two
refused records and nine metadata-only records; 195 mapped methods and 9,745
position entries are retained. Read-only review found no concrete defect.

Tests, generated-JS execution, benchmarks and RSS tracking remain paused by
the user. Build and ABC-only output inspection do not establish full behavioral
equivalence or the process memory ceiling. Deferred regressions include live
acc successors, loop backedges, transparent cycles, throwing getters/setters,
aliased and null bases, receiver replacement, all owner barriers, multiple
folds, predicate origins, PC spans, resource limits and 200/201 scaling.
