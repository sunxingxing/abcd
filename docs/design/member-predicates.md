# Member Predicate Recovery

## Scope

Extend final-AST expression recovery to a named-property read stored in a
temporary and then tested by strict equality against a primitive constant.
The new MemberComparison is a typed PredicateInput, not generated-text parsing.
Its left operand retains MemberExpression, its comparison is StrictEquality,
and its source InstructionId retains the property-read origin.

```js
$r5 = params.pathStack;
if ($r5 !== void 0) { /* existing body */ }
```

becomes a direct `params.pathStack !== void 0` condition only when the final
emitted temporary is dead after the branch. This does not merge the independent
property read inside the body: a getter may return a different value there.

The producer and branch must be in the same basic block, with only emitted
no-ops between them. The producer is a plain named-property read from a private
register; the branch is an already admitted Boolean predicate with typed strict
equality/inequality input. The right operand is a primitive numeric/special
constant, not a register, allocation, property or coercing comparison. Moving
the GetValue across empty anchors neither duplicates the getter nor crosses an
observable evaluation. Self-reading producer slots are conservatively excluded
by the write-only access requirement. Complex origins and argument owners are
barriers; catches, phased SSA and resume edges remain outside this pass.

## Shared Analysis

Accumulator liveness is now SlotLiveness, parameterized by a typed ScalarSlot.
The same backwards may-live algorithm serves field RHS composition and member
predicates. Register operands use the existing exhaustive register-read audit;
scalar/local/module stores and own-property receiver reads are accounted for.
Unknown protocols still mean a possible read. Predicate and member-store fused
origins only relocate PC spans; their actual typed operands determine reads.

This slice selects the first candidate register in each method and performs
one liveness analysis for all its candidate definitions. It deliberately does
not loop over all registers and run whole-graph analysis for each. Selection and
application are linear scans with constant candidate state. Overall time and
scratch remain O(I+B+E), using the existing admitted liveness budget. Other
registers remain unmodified. This is bounded incremental recovery, not a claim
that general register live-range splitting is complete.

The accumulator pass finishes and releases its analysis storage before this
analysis starts. There is no simultaneous per-slot cache or new dependency.
The shared algorithm is ordinary backwards may-liveness; its unknown-owner
barriers and capacity checks remain binding.

## Printing And Origins

The printer emits MemberComparison through existing Expression/Value binding
APIs, so parameter aliases and slot census still apply. The old producer becomes
a zero-width AST anchor, then its source PC is relocated to the generated
predicate span. Existing comparison and reload PC spans remain attached too.
Both ordinary branch printing and structured if/loop printing use that same
typed predicate and relocation path. No filename, register spelling or method
name participates in the proof.

Both production CLI targets build. ABC-only pasteboard export removes 23
temporary-register assignments and six declarations. All six page
setInitiallyProvidedValue methods now use direct parameter-property conditions;
ProgressBar's progress comparison also folds. Publication remains eight
incomplete sources, two refused records, nine metadata-only records and 195
mapped methods / 9,745 position entries. Read-only review found no concrete
defect. These outputs do not establish full application equivalence.

Tests, generated-JS execution and RSS tracking remain paused. Deferred
regressions include side-effectful getters, branch-live temporaries, loop-carried
reads, self-reading producers, multiple candidate slots, owner boundaries,
conditional expression printing, fused field origins, PC mappings, memory
admission and 200/201 scaling.
