# Closure Slot Audit

## Ownership Proof

FunctionExpression and CapturedExpression no longer disqualify the whole body
from bound-read and VM-slot usage auditing. Neither consumes a caller register
identifier outside Scalar/Value/Target:

- WriteLeafFunction receives MethodConstants and a creation descriptor, not
  the caller's ScalarNames. The child builds its own ABI/register bindings.
- LeafBuffer resolves every capture through ResolveCreationCapture into a
  typed local cell or outer capture. LocalName/Capture never refer to caller
  VM registers. The original environment, initialization, mutation barrier and
  capture-domain obligations remain mandatory at the same creation site.
- CapturedExpression retains its typed zero-width capture occurrence. This
  change does not discard a capture or substitute a lexical value snapshot.
- Stores that populate captured cells still print their input through Slot or
  Expression. If a cell receives a current-function/register value, that input
  is observed; the audit cannot erase the necessary source VM binding.

Class heritage remains an identifier-only consumer, excluded from bound ABI
substitution. Only the separately audited record mode permits it. Method,
accessor, super, exception-region and other unaudited alternatives retain their
conservative behavior. No generated text participates in this proof.

## Census Without Substitution

PrepareEntryBindings now separates two decisions. this/formal/rest information
licenses optional FindEntryBindings substitution; the complete typed AST audit
licenses slot-use collection. A normal method without substitution information
can still get accurate usage metadata with an empty entry-binding table. Its
register copies remain unchanged, since IsBoundEntryCopy cannot find a binding.
Each actual register read or assignment target remains observed.

This matters for zero-parameter arrow functions and small callbacks: an unused
incoming function-object slot no longer forces a generated self declaration.
Existing SelfBindingUse consumers can also remove class method self captures
when the complete body audit proves them unused, including closure-heavy
methods. Unknown methods still keep their captures and class wrappers.

## Complexity and Scope

The AST audit is linear in AST size. Existing entry-binding proof and slot-use
bitmap admission remain unchanged; no per-capture register scan, recursive
parent-register analysis, new allocation class, or extra body emission pass was
added. Capture resolution retains its existing indexed proof lookups. The
implementation uses existing project types and algorithms, with no new pinned
dependency or licensing change.

## Production Evidence

Production abc_semantic/abc_structure builds succeeded. ABC-only export
`build/pasteboard-closure-slots-01` contains 7,112 lines / 127,462 bytes versus
7,956 / 137,068 in `build/pasteboard-record-slots-01`, a reduction of 844 lines
and 9,606 bytes across all six pages. CommonConstants and Logger sizes remain
unchanged. PageA.initialRender now keeps its lexical this capture without an
unused method-function register; its class no longer needs a self wrapper.
Home and PasteBoard still retain wrappers for unaudited method bodies.

Statuses remain eight incomplete records, two refused constructors at PC 14,
and nine metadata records. Partial maps retain 195 methods / 9,745 spans;
removed copies retain zero-width anchors, for example PageA method 7505
PCs 0/4/6 at byte 5171. These totals are coverage evidence, not equivalence proof.

Independent read-only review found no concrete correctness issue. Tests,
generated JS execution and RSS checks remain paused by user request. Deferred
regressions include captured parent self/this, mutable captured cells, stronger
capture domains, zero-parameter callbacks, absent ABI hints, used new.target,
class heritage, exception fallback, and tight metadata budgets. Full source
equivalence and the process peak-memory cap remain unverified.
