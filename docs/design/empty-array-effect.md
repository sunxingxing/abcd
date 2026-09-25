# Empty Array Allocation Effect

EmptyArray remains a compact ScalarValue, but its SemanticEffects::normal now
includes LanguageEffect::kAllocation. Syntax representation does not imply
purity: evaluating it creates a fresh identity, so generic pure-copy admission
must not allow duplication or removal of that evaluation. No new semantic
string tag, dependency, traversal or allocation is introduced by this effect
classification.

The local ets_runtime checkout was verified at OpenHarmony-v7.0-Release. Its
interpreter-inl.cpp CREATEEMPTYARRAY_IMM8/IMM16 handlers at lines 2523-2535 call
SlowRuntimeStub::CreateEmptyArray, then SET_ACC and DISPATCH without an abrupt
completion check. RuntimeCreateEmptyArray in stubs/runtime_stubs-inl.h at line
2268 obtains the realm array constructor and calls NewJSObjectByConstructor.
This change records normal allocation without inventing a new ordinary
exception edge. The source engine's approved resource-exhaustion exclusion is
unchanged; this is not a proof that VM allocation cannot exhaust resources.

FoldFieldValue now expressly permits kAllocation only for its independently
category-checked EmptyArray case under the V2 resource contract. Other eligible
RHS values still require kNone, and exceptional/pre-throw effects remain
disallowed. Existing allocation identity, SSA sole-use and receiver-order
proofs are not replaced by this flag. Other generic pure-effect consumers
become conservative automatically.

abc_semantic --instructions additionally serializes normal_effects and
before_throw_effects as the numeric typed flags, beside exceptional_completion.
Read-only inspection of real Home method 6016 shows PC 30 with allocation flag
64, PC 32 with no effects, and PC 34 with heap-write/user-code flags 6 and a
possible exception. These numbers are output labels, not engine magic constants.

Production abc_semantic/abc_structure builds succeeded. ABC-only export
`build/pasteboard-allocation-effect-01` has byte-identical sources and position
maps to `pasteboard-array-field-values-01`. Statuses remain eight incomplete,
two constructors refused at PC 14 and nine metadata-only records. Independent
read-only review found no concrete issue. Tests, JS execution and RSS remain
paused. Deferred regressions include both instruction widths, fresh identity,
pure-copy rejection, explicit field-fold admission and resource-contract
fallback. Whole-source equivalence and memory-cap compliance remain unverified.
