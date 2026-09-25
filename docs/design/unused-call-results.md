# Unused Call Results

ElideUnusedResults (originally ElideCallResults) is a final shared AST annotation pass. It preserves each
ordinary call, member call, constructor call and staged receiver call, while
omitting an accumulator commit proven unobserved by the existing SSA counts.
It is not call elimination and never assumes a callee is pure.

Only normal SSA without catch regions participates. For SourceAssignment the
target must still be the accumulator and CanRetargetExpression must approve
the source-origin ownership. Register-retargeted assignments remain unchanged.
The exact original instruction's SSA output must be an accumulator definition
with zero uses. Counts retain phi inputs and account only for previously
certified consumed reads and removed dead copies.

A SourceStagedCall lives at the property-read PC but its result is defined at
call_statement. Use that original call coordinate, never the getter's output.
Nested calls and retained parameter reads stay counted. The printer consumes
AssignmentResult::kUnused by omitting only the assignment prefix. It retains
property Reference evaluation, arguments, callee effects and exceptions. Native
argument mode rejects this marker; argument-only result emission remains a
different proof. Sequence mode continues to emit the retained call expression.

PC spans and fused-origin relocation continue through the same Statement
writer. No source text is searched and no effects move across instructions.
The pass uses O(N) traversal, O(C log(N + U)) normal-SSA lookup for C calls and
U reads, and O(1) extra scratch. Added typed node state is charged by existing
sizeof-based AST admission; no duplicate use counts or expression trees exist.

Independent read-only review found no concrete correctness issue. Tests,
generated JS execution and RSS checks remain paused. Deferred regressions:
throwing getters/callees, live nested-call results, receiver/argument aliases,
phi-observed results, register-retargeted fallback, constructor side effects,
sequence grammar, argument-mode rejection, and fused/getter/call PC mappings.
Behavioral equivalence remains unproven without those checks.

## Production Evidence

Production targets abc_semantic and abc_structure built successfully. ABC-only
export `build/pasteboard-unused-call-results-01` totals 8,118 lines / 140,675
bytes, compared with 8,118 / 143,524 in `build/pasteboard-receiver-uses-01`.
Seven records have shorter source; CommonConstants is unchanged. Logger's
logging calls now appear as expression statements rather than accumulator
assignments, retaining all four native argument expressions.

Statuses remain eight incomplete source records, two refused constructors at
PC 14 and nine metadata records. Partial maps retain 195 methods and 9,745
spans. No tests, generated JS execution or RSS measurements were run.

## General Expression Results

The same pass now admits scalar/property expressions, comparisons, module,
global, realm and lexical/captured reads, and empty/primitive-buffer aggregate
creation. HasStandaloneValue exhaustively enumerates admitted SourceExpression
alternatives. The source grammar is audited separately from effect purity:
objects remain parenthesized, arrays and scalar expressions remain valid
expression statements, and capture tokens still require their original owner.
No getter, Proxy operation, coercion, allocation or exception is removed.

StringExpression remains excluded because a standalone string can become a
directive. Callable/class naming and super/rest/await owner protocols are not
admitted by this change. All original SSA, normal-flow, target and provenance
checks remain; no use count is decremented merely because a commit disappears.
The following module-receiver fold requires an exact single use, which does not
overlap the zero-use case here. Complexity is O(N + E log(N + U)) for E candidate
expressions and U reads, with constant extra scratch and no new dependencies.

Production builds and ABC-only export `build/pasteboard-unused-results-01`
succeeded. Logger now emits `$r5.prototype;` instead of assigning the unused
read result to `$acc`; CommonConstants has two analogous changes. All six page
outputs are unchanged. Totals are 6,618 lines / 121,555 bytes versus 121,576
bytes in `build/pasteboard-callback-arguments-01`. These reads deliberately
remain: unused values alone do not prove absence of getter/Proxy effects.

Statuses remain eight incomplete records, two constructors refused at PC 14,
and nine metadata records; partial maps retain 195 methods / 9,745 spans.
Independent read-only review found no concrete correctness issue. Tests,
generated JS and RSS remain paused. Deferred regressions additionally include
throwing property reads, effectful numeric conversion, captured-cell reads,
allocation identity, leading string directives and adjacent module receivers.
This output evidence does not establish full source equivalence or memory-cap
compliance.
