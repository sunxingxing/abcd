# SSA-Proven Argument Expressions

FoldArgumentExpressions is a final shared AST annotation pass, after existing
expression/return folds. It consumes the ordered native-argument certificate
and exact SSA use counts already computed by BuildSourceAst. It never matches
printed source, opcode names or pasteboard-specific method names.

An assignment receives AssignmentResult::kArgumentOnly only when its original
register definition has exactly one remaining SSA use, and that value is the
corresponding argument read at the original receiver call. Counts include phi
inputs. Uses removed by the earlier sparse dead-copy pass are already deducted.
Phased SSA and catches remain excluded. The branch-region-owning call remains
excluded, but other calls in the same method may qualify independently.

Direct register definitions are checked against the target. For a retargeted
accumulator expression, the following NoOperation must carry the existing
previous_origin certificate. The original instruction there must be a normal,
effect-free accumulator-to-target save with exactly one read of the producer's
sole-use accumulator definition. The tested register value is the save's SSA
definition, never the producer accumulator. Merged expression origins not
covered by this proof retain their assignments.

Receiver-call argument reads are the suffix of its register-effect reads
(EffectOf(ReceiverCall) in src/analysis/register_effect.cc). Each corresponding
SSA value's slot and definition are checked; argument counts and original
call operands must also agree. The pass makes no per-argument use-list scans.

The printer consumes the typed result only in StatementForm::kArgument and
rejects it elsewhere. It omits the target commit, not RHS evaluation. Original
property lookup, argument ordering and exceptions remain unchanged. Statement
and save-anchor spans still use the shared position writer. Slot observation
now excludes the removed target but still visits every emitted operand, so
unused declarations disappear through the existing audited mechanism.

For N AST statements, A candidate assignments and U SSA reads, structural
scanning is O(N + A); normal SSA lookup adds O(A log(N + U)) in the worst case.
Nested staged calls terminate the parent's ordered-prefix check. O(1) extra
scratch is used; the typed field is included in existing sizeof-based AST
admission. No graph, use index or expression tree is duplicated.

Tests, generated JS execution and RSS measurement remain paused. Deferred
regressions must cover direct/retargeted definitions, repeated register reuse,
additional normal and phi uses, later argument dependencies, throwing getters,
merged-origin fallback, nested/branch/exception fallback, output-mode rejection
and generated coordinates after target removal. Completion and behavioral
equivalence remain unproven without those checks.

## Production Evidence

Both production targets built successfully after replacing an invalid whole
ScalarSlot comparison with an explicit RegisterId alternative check. ABC-only
export `build/pasteboard-argument-expressions-01` contains 8,404 lines /
146,729 bytes, compared with 8,420 / 146,977 in the preceding ordered-argument
export. Logger's four logging methods eliminate 16 argument commits and their
declarations, now printing field reads directly as arguments. Other record
source totals are unchanged.

Statuses remain eight incomplete source records, two refused constructors at
PC 14 and nine metadata records. Partial method maps retain 195 methods and
9,745 spans. Independent read-only review found no concrete correctness issue.
No tests, generated JS execution or RSS measurements were run.

## Per-Call Region Admission

FoldArgumentExpressions now matches the printer's exact region ownership test:
the call whose InstructionId equals SourceArgumentRegion::call is skipped.
Other calls still require the complete ordered-assignment interval and exact
SSA argument-definition proof. An outer interval containing a nested staged
call fails the ordered grammar; the nested call may qualify on its own. Every
marked assignment must therefore be emitted through StatementForm::kArgument,
never as an IIFE statement or comma-sequence commit.

No new traversal/storage or exception semantics are introduced. In ABC-only
`build/pasteboard-branch-calls-01`, 11 callback commits disappear across the six
pages compared with `pasteboard-branch-receivers-01`, saving 66 bytes. Source
line counts, record statuses and method/span counts remain unchanged. Production
builds and read-only review completed; tests, JS execution and RSS remain
paused. Deferred cases include straight-line calls before/after a region,
nested calls inside its branches, live callback targets and owner-call fallback.
