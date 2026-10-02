# Shared numeric facts

The common value analysis distinguishes FiniteNonzeroNumber, FiniteNumber
(including both signed zeros), NonNaNNumber, Defined and Language. Numeric
joins keep finite information when both inputs are finite and drop nonzero
information when zero is possible. No source/debug spelling supplies a fact.

A separate `ExcludesInt32MinTag` witness describes the original VM value's
representation. Integer literals other than INT32_MIN and non-NaN FLDAI doubles
supply it. A double numerically equal to INT32_MIN is not the tagged int32
minimum. Joins retain this witness only when every alternative has it. Numeric
identity conversion preserves the incoming witness; updates and arithmetic do
not inherit it merely because their inputs have it.

ADD2 can produce a proved non-NaN Number when both inputs are non-NaN Numbers
and at least one is finite: infinity cancellation cannot occur. String addition
continues to require Language inputs with a proved String operand. SUB2 uses
the same numeric criterion and also requires the RHS tag exclusion. Pinned
`interpreter-inl.cpp` (OpenHarmony-v7.0-Release, lock identity) first negates an
int32 RHS before checking overflow; negating INT32_MIN is undefined C++. A
source-level UBSan probe establishes this limitation, not actual Ark VM output.
Do not silently treat that case as native JS subtraction or authorize it from a
Language-only input. The original noncanonical-NaN counterexamples still refuse.

Normal and phased SSA use the same sparse worklist transfer rules; linear
lexical analysis calls the same scalar helpers. ToNumber preserves all facts of a proved non-NaN Number; otherwise a Language
input has a Defined normal result. The pinned conversion returns Number or
throws, including BigInt/Symbol and conversion-hook failures; it does not imply
non-NaN output. ToNumeric has the same Defined normal-result bound while retaining BigInt
semantics. Known non-NaN Number input uses identity; other inputs use exclusive
postfix scratch under the resource-exclusion contract. Independent verification
checks the old-value result and rejects reads/captures/reuse of that scratch.
V1 does not authorize the extra numeric operation. This does not admit arbitrary
arithmetic over conversion results. See ECMAScript 13.4.2.1 for the returned
ToNumeric oldValue; this is distinct from platform differential acceptance.

The fixed-height lattice adds one finite refinement and one independently
weakening exclusion. Phi joins stay incremental; arithmetic nodes inspect at
most two operands, with no per-variable CFG walk or interval fixed-point engine.
Existing allocation accounting uses actual ValueKinds size. Work-counter tests
are not physical RSS or whole-corpus performance evidence.

Decoded tests cover integer overflow to double, signed zeros, subnormals,
finite extremes, infinities, NaN rejection, normal/phased results, identity
conversion and update-created tagged minima. Independent native JS compares
actual generated results with Object.is. These component checks do not replace
full record, platform or source-style acceptance.

## Modern and deprecated negation

The semantic model retains `kLegacyNegate` separately from `kNegate`. Pinned
modern NEG explicitly promotes tagged INT32_MIN to a double result; deprecated
NEG lacks that guard. Legacy lowering therefore requires non-NaN Number input
and ExcludesInt32MinTag. Both proved forms preserve finite/nonzero facts and
produce a representation excluding tagged INT32_MIN. The common forest and
independent operand verifier recheck the legacy premise, including literal
operands exposed by copy propagation. The old standalone scalar formatter has
no such witness and refuses the legacy operation. This is conditional native
lowering, not blanket admission of every input or an alias of opcode identity.
