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
lexical analysis calls the same scalar helpers. ToNumber/ToNumeric currently
preserve all facts of a proved non-NaN Number instead of discarding finite/tag
information. This is not admission of arbitrary Number/BigInt coercions.

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
