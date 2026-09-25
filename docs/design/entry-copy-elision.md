# Entry Copy Elision

SSA-proven immutable entry bindings can now omit redundant initialization in
the shared printer, but only after an exhaustive typed AST audit proves that
every supported register read uses the bound Value/Slot path. This is distinct
from declaration liveness: declarations remain emitted.

The audit admits scalar/member/string/local/module expressions, ordinary and
member calls, constructor calls and basic statements with bound reads. It
rejects creation/capture/global-name/buffer/rest/await/super expressions and
staged/array/unsupported stores, plus catch and argument regions. Any rejected
node preserves entry copies for the whole method. New AST alternatives must
be classified explicitly through compile-time exhaustive visitors.

An omitted statement must be a register-to-register copy whose destination
has the existing unique-definition/all-reads SSA certificate and whose source
is that certificate's immutable ABI input. Merged origins and comma-expression
contexts are excluded. The printer skips only the statement's text; its outer
PC-span recording still executes, preserving a zero-width source anchor.
There is no generated-text inspection, allocation/effect movement or mutation
of original IR/CFG/SSA.

The audit costs O(I) time and fixed scratch, with O(1) entry-table lookup per
candidate. No per-variable graph walk or additional retained buffer is added.
Methods with opaque identifier consumers retain initialized registers, rather
than relying on the absence of a name in generated JS text.

Both production targets built successfully. ABC-only pasteboard export
`build/pasteboard-entry-elision-01` removes 69 transport lines, yielding 9,441
lines / 160,551 bytes. BreakpointType's constructor directly assigns fields
from sm/md/lg; GetValue no longer initializes receiver/parameter copies before
its comparisons. Publication remains eight incomplete app records, two refused
host constructors at PC14 and nine metadata records. Method maps retain 195
method entities / 9,745 spans. Read-only review found no concrete issue.

Tests, generated JS execution and RSS measurements remain paused. Deferred
regressions include every audit alternative, new AST alternatives, opaque
capture/class consumers, early reads, multiple writes/phis, comma contexts,
zero-width PC anchors and geometric input scaling. These changes do not prove
full source equivalence or eliminate all unused declarations.
