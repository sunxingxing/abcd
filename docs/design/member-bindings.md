# Binding-Based Property Expressions

Ordinary MemberExpression now accepts a typed optional MemberReceiver, reusing
the BindingReceiver certificate previously used by staged method calls. The
receiver is a module/global/lexical/captured binding or private register, never
a printed expression or semantic string. Its source and optional module guard
retain original instruction IDs, as does the property read itself.

FoldBindingReceivers remains the common admission pass. It requires one normal
basic block without catches, a stored accumulator binding load, and the next
emitted operation to be the original named property read. The AST and original
property names agree. Intervening nodes must be erased pure copies or the
already-proven immediately following module HoleGuard. Exact SSA accounting
allows only the property consumer, the certified guard and sole-use erased
save/reload pairs. Phi and other observations still prevent folding. No binding
lookup, TDZ check, getter/Proxy effect, capture-domain obligation or exception
is discarded or reordered. Ordinary reads do not gain call receiver semantics.

The printer shares BoundReceiver between calls and member expressions. Register
reads go through Value, including ABI substitution and slot observation; all
other alternatives use existing Expression dispatch and its binding checks.
Source and guard PCs map to the actual emitted receiver span. Property PCs
retain their statement span. The property coordinate is explicit, never inferred
from generated text. Invalid source distances and PC identities still reject.

The pass runs after expression/return wrapping; later object folds cannot match
a member expression, and argument folding only annotates its result. A future
pass moving this node must update its origin contract first. This is bounded
copy propagation, not recursive expression substitution or heap alias analysis.
Disjoint producer-to-property gaps are scanned once, with indexed SSA queries:
O(I log(I + U)) for I instructions and U reads, constant scratch. Inline
receiver storage is charged by existing sizeof(SourceStatement) admission.
No dependency or new execution premise is introduced.

Tests, generated JS execution and RSS remain paused. Deferred regressions cover
binding/name mismatches, intervening effects, getter throws, extra SSA/phi uses,
guard origins, ABI registers, capture domains, nested argument spans, output
limits and preservation of existing staged-call output. This does not establish
full source equivalence, complete record recovery or measured memory compliance.

## Production Evidence

Both production CLIs build. ABC-only export `build/pasteboard-member-bindings-01`
recovers direct expressions such as Column.justifyContent(FlexAlign.End),
Column.height($import1.FULL_PERCENT), and the three native createStorageProp
arguments. Captured receivers likewise appear directly in GetValue arguments.
All six pages change; totals become 5,086 lines / 99,948 bytes, down 304 lines /
3,079 bytes. CommonConstants and Logger source texts remain unchanged.

PageA method 7255 module-load PC 82 and guard PC 84 map to bytes [5258,5266),
the `$import1` receiver. Property PC 92 and its removed save PC 96 map to
[5258,5279), `$import1.FULL_PERCENT`. Partial maps retain 195 methods / 9,745
spans. Statuses remain eight incomplete, two host constructors refused at PC 14
and nine metadata-only records. Read-only review and its follow-up found no
concrete defect. The checks above are build and generation evidence only;
tests, JS execution and RSS were not run.
